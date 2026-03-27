/* Start Header ***************************************************************/
/*!
\file client_main.cpp  (Spaceships game client – Assignment 5)
\author TODO
\par email: TODO
\date 27 Mar, 2026
\brief
  Game client for the Spaceships multiplayer LAN game.
  - Pure UDP communication with the authoritative server.
  - Reads server address from config.txt (or prompts at startup).
  - Renders game state to the Windows console using ASCII art.
  - Handles input, chat, reconnect, and reliable ACK for critical events.
  - Displays live scores and the final top-5 leaderboard.

Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited. */
/* End Header *****************************************************************/

#include "../common/game_protocol.h"
#include "../common/game_math.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
  #include <conio.h>
  #include <windows.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

// ── Config ────────────────────────────────────────────────────────────────────
static std::string g_serverIP   = "127.0.0.1";
static uint16_t    g_serverPort = GAME_PORT;
static uint16_t    g_clientPort = GAME_PORT_CLIENT;
static char        g_playerName[16] = "Player";

// ── Client state ─────────────────────────────────────────────────────────────
static SOCKET     g_sock  = INVALID_SOCKET;
static sockaddr_in g_serverAddr{};
static std::atomic<bool> g_running{true};

static uint8_t  g_myId = 0;
static bool     g_joined = false;
static uint32_t g_lastServerTick = 0;
static uint32_t g_localTick = 0;

// Received game state (written by recv thread, read by render thread)
static std::mutex g_stateMtx;

struct ClientPlayer {
    float    x = 0, y = 0;
    float    angle = 0;
    uint16_t score = 0;
    uint8_t  alive = 0;
    uint8_t  lives = 0;
    char     name[16]{};
    bool     active = false;
};

struct ClientAsteroid {
    float   x = 0, y = 0;
    uint8_t size = 0;
    bool    alive = false;
    uint8_t id = 0;
};

struct ClientBullet {
    float   x = 0, y = 0;
    uint8_t ownerId = 0;
    bool    alive = false;
};

static std::array<ClientPlayer, MAX_PLAYERS>   g_players{};
static std::array<ClientAsteroid, MAX_ASTEROIDS> g_asteroids{};
static std::array<ClientBullet, MAX_BULLETS>   g_bullets{};
static uint8_t g_playerCount = 0;
static uint8_t g_asteroidCount = 0;
static uint8_t g_bulletCount = 0;

// Scores shown in HUD
struct ScoreEntry { uint8_t id; uint16_t score; char name[16]; };
static std::array<ScoreEntry, MAX_PLAYERS> g_scores{};
static int g_scoreCount = 0;

// Round-over data
static bool g_roundOver = false;
static std::vector<std::string> g_resultLines;

// Chat log
static std::vector<std::string> g_chatLog;
static std::mutex g_chatMtx;

// Ping
static float    g_pingMs = 0.0f;
static uint32_t g_pingSeq = 0;

// Reliable ACK tracking
static std::map<uint16_t, MsgType> g_pendingAcks; // seq -> type we need to ACK

// Input firing rate limiter
static uint32_t g_lastFireTick = 0;

// ── Config loading ────────────────────────────────────────────────────────────
static void LoadConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (!val.empty() && val.back() == '\r') val.pop_back();
        if (key == "SERVER_IP")    g_serverIP = val;
        if (key == "SERVER_PORT")  g_serverPort = static_cast<uint16_t>(std::stoi(val));
        if (key == "CLIENT_PORT")  g_clientPort = static_cast<uint16_t>(std::stoi(val));
        if (key == "PLAYER_NAME")  strncpy_s(g_playerName, sizeof(g_playerName), val.c_str(), _TRUNCATE);
    }
}

// ── Console helpers ───────────────────────────────────────────────────────────
#ifdef _WIN32
static void ClearScreen() {
    COORD coord = {0, 0};
    DWORD count;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacter(h, ' ', cells, coord, &count);
    FillConsoleOutputAttribute(h, csbi.wAttributes, cells, coord, &count);
    SetConsoleCursorPosition(h, coord);
}
static void HideCursor() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci; ci.dwSize = 1; ci.bVisible = FALSE;
    SetConsoleCursorInfo(h, &ci);
}
static bool KbHit() { return _kbhit() != 0; }
static int  GetKey() { return _getch(); }
#else
static void ClearScreen() { std::cout << "\033[2J\033[1;1H"; }
static void HideCursor()  { std::cout << "\033[?25l"; }
static bool KbHit() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = -1;
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    struct timeval tv{0, 0};
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch != -1;
}
static int GetKey() { return getchar(); }
#endif

// ── Render frame buffer ───────────────────────────────────────────────────────
// We render into a char grid, then print it all at once to minimise flicker.
static constexpr int RENDER_W = 80;
static constexpr int RENDER_H = 30;
static char g_frame[RENDER_H][RENDER_W + 1];

static void ClearFrame() {
    for (int y = 0; y < RENDER_H; ++y) {
        std::memset(g_frame[y], ' ', RENDER_W);
        g_frame[y][RENDER_W] = '\0';
    }
}

static void PutChar(int px, int py, char c) {
    // Map world coordinates to render grid
    int rx = static_cast<int>(px * RENDER_W / WORLD_W);
    int ry = static_cast<int>(py * RENDER_H / WORLD_H);
    if (rx < 0 || rx >= RENDER_W || ry < 0 || ry >= RENDER_H) return;
    if (c > g_frame[ry][rx] || g_frame[ry][rx] == ' ') // priority: non-space wins
        g_frame[ry][rx] = c;
}

static void PutStr(int rx, int ry, const char* s) {
    if (ry < 0 || ry >= RENDER_H) return;
    for (int i = 0; s[i] && rx + i < RENDER_W; ++i)
        g_frame[ry][rx + i] = s[i];
}

static void PrintFrame() {
    // Move cursor to top-left without clearing (reduces flicker)
#ifdef _WIN32
    COORD c = {0, 0};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
#else
    std::cout << "\033[1;1H";
#endif
    for (int y = 0; y < RENDER_H; ++y)
        std::cout << g_frame[y] << '\n';
    std::cout.flush();
}

// ── Draw ship (simple triangle using surrounding chars) ───────────────────────
static void DrawShip(float x, float y, float angleDeg, bool isMe, char label) {
    float rad = DegToRad(angleDeg);
    char c = isMe ? '@' : label;
    PutChar(static_cast<int>(x), static_cast<int>(y), c);
    // Draw a nose indicator
    int nx = static_cast<int>(x + std::cos(rad) * 10);
    int ny = static_cast<int>(y + std::sin(rad) * 10);
    PutChar(nx, ny, isMe ? '*' : '.');
}

// ── Draw asteroid ─────────────────────────────────────────────────────────────
static void DrawAsteroid(float x, float y, uint8_t size) {
    char c = (size == 3) ? 'O' : (size == 2) ? 'o' : '.';
    PutChar(static_cast<int>(x), static_cast<int>(y), c);
}

// ── Draw bullet ──────────────────────────────────────────────────────────────
static void DrawBullet(float x, float y) {
    PutChar(static_cast<int>(x), static_cast<int>(y), '-');
}

// ── Render loop ───────────────────────────────────────────────────────────────
static void RenderLoop() {
    HideCursor();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps

        ClearFrame();

        // Draw border
        for (int x = 0; x < RENDER_W; ++x) { g_frame[0][x] = '-'; g_frame[RENDER_H-1][x] = '-'; }
        for (int y = 0; y < RENDER_H; ++y) { g_frame[y][0] = '|'; g_frame[y][RENDER_W-1] = '|'; }

        {
            std::lock_guard<std::mutex> lock(g_stateMtx);

            if (!g_joined) {
                PutStr(2, RENDER_H/2, "Connecting to server... Press Ctrl+C to quit.");
                PrintFrame();
                continue;
            }

            if (g_roundOver) {
                int row = 3;
                PutStr(RENDER_W/2 - 10, row++, "===  ROUND OVER  ===");
                for (auto& line : g_resultLines) {
                    if (row >= RENDER_H - 4) break;
                    PutStr(4, row++, line.c_str());
                }
                PutStr(2, RENDER_H - 3, "Press R to play again or Q to quit.");
                PrintFrame();
                continue;
            }

            // Draw entities
            for (int i = 0; i < g_asteroidCount; ++i) {
                auto& a = g_asteroids[i];
                if (a.alive) DrawAsteroid(a.x, a.y, a.size);
            }
            for (int i = 0; i < g_bulletCount; ++i) {
                auto& b = g_bullets[i];
                if (b.alive) DrawBullet(b.x, b.y);
            }

            char playerLabel = 'A';
            for (int i = 0; i < g_playerCount; ++i) {
                auto& p = g_players[i];
                if (!p.active || !p.alive) { ++playerLabel; continue; }
                bool isMe = (p.name[0] != '\0' && std::strncmp(p.name, g_playerName, 15) == 0);
                DrawShip(p.x, p.y, p.angle, isMe, playerLabel);
                ++playerLabel;
            }

            // HUD – top right area (scores)
            char hudBuf[64];
            for (int i = 0; i < g_scoreCount && i < 6; ++i) {
                std::snprintf(hudBuf, sizeof(hudBuf), "%s:%u", g_scores[i].name, g_scores[i].score);
                PutStr(RENDER_W - 20, 1 + i, hudBuf);
            }

            // HUD – bottom: controls
            PutStr(2, RENDER_H - 3, "WASD/Arrows=Move  Space=Fire  T=Chat  Q=Quit");
            std::snprintf(hudBuf, sizeof(hudBuf), "Ping:%.0fms  Tick:%u", g_pingMs, g_localTick);
            PutStr(2, RENDER_H - 2, hudBuf);

            // Chat log (last 3 lines)
            {
                std::lock_guard<std::mutex> cLock(g_chatMtx);
                int startLine = static_cast<int>(g_chatLog.size()) - 3;
                if (startLine < 0) startLine = 0;
                for (int i = startLine, row = 1;
                     i < static_cast<int>(g_chatLog.size()) && row < 4; ++i, ++row) {
                    PutStr(2, row, g_chatLog[i].c_str());
                }
            }
        }

        PrintFrame();
    }
}

// ── Network send helpers ──────────────────────────────────────────────────────
static void SendTo(const void* data, int len) {
    sendto(g_sock, reinterpret_cast<const char*>(data), len, 0,
           reinterpret_cast<const sockaddr*>(&g_serverAddr), sizeof(g_serverAddr));
}

static void SendJoin() {
    PktJoin pkt{};
    pkt.hdr.type     = MSG_JOIN;
    pkt.hdr.playerId = 0;
    pkt.hdr.seq      = htons(SEQ_UNRELIABLE);
    pkt.hdr.timestamp = htonl(g_localTick);
    strncpy_s(pkt.name, sizeof(pkt.name), g_playerName, _TRUNCATE);
    SendTo(&pkt, sizeof(pkt));
}

static void SendInput(uint8_t keys) {
    PktInput pkt{};
    pkt.hdr.type     = MSG_INPUT;
    pkt.hdr.playerId = g_myId;
    pkt.hdr.seq      = htons(SEQ_UNRELIABLE);
    pkt.hdr.timestamp = htonl(g_localTick);
    pkt.keys         = keys;
    SendTo(&pkt, sizeof(pkt));
}

static void SendAck(uint16_t seq) {
    PktAck pkt{};
    pkt.hdr.type     = MSG_ACK;
    pkt.hdr.playerId = g_myId;
    pkt.hdr.seq      = htons(SEQ_UNRELIABLE);
    pkt.hdr.timestamp = htonl(g_localTick);
    pkt.ackSeq       = htons(seq);
    SendTo(&pkt, sizeof(pkt));
}

static void SendQuit() {
    PktHeader pkt{};
    pkt.type     = MSG_QUIT;
    pkt.playerId = g_myId;
    pkt.seq      = htons(SEQ_UNRELIABLE);
    SendTo(&pkt, sizeof(pkt));
}

static void SendPing() {
    using Clock = std::chrono::steady_clock;
    PktPing pkt{};
    pkt.hdr.type = MSG_PING;
    pkt.hdr.seq  = htons(static_cast<uint16_t>(++g_pingSeq));
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                       Clock::now().time_since_epoch()).count();
    pkt.clientTime = now; // we don't bother with htonll – it's echoed back as-is
    SendTo(&pkt, sizeof(pkt));
}

// ── Receive thread ────────────────────────────────────────────────────────────
static void RecvThread() {
    using Clock = std::chrono::steady_clock;
    char buf[8192];
    sockaddr_in from{};

    while (g_running) {
        int fromLen = sizeof(from);
        int r = recvfrom(g_sock, buf, sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (r < static_cast<int>(sizeof(PktHeader))) continue;

        PktHeader hdr;
        std::memcpy(&hdr, buf, sizeof(PktHeader));
        uint16_t seq = ntohs(hdr.seq);

        // If this is a reliable message, schedule an ACK
        if (seq != SEQ_UNRELIABLE) {
            SendAck(seq);
        }

        switch (static_cast<MsgType>(hdr.type)) {

        case MSG_JOIN_ACK:
        case MSG_REJOIN_ACK: {
            PktJoinAck ack;
            std::memcpy(&ack, buf, sizeof(PktJoinAck));
            std::lock_guard<std::mutex> lock(g_stateMtx);
            g_myId   = ack.assignedId;
            g_joined = true;
            std::cout << "[Client] Joined! My player ID = " << (int)g_myId << "\n";
            break;
        }

        case MSG_JOIN_DENY: {
            std::cout << "[Client] Server denied join (full or game over).\n";
            g_running = false;
            break;
        }

        case MSG_GAME_STATE: {
            PktGameState gs;
            std::memcpy(&gs, buf, sizeof(PktGameState));
            std::lock_guard<std::mutex> lock(g_stateMtx);
            g_lastServerTick  = ntohl(gs.gameTick);
            g_playerCount     = gs.playerCount;
            g_asteroidCount   = gs.asteroidCount;
            g_bulletCount     = gs.bulletCount;

            for (int i = 0; i < gs.playerCount; ++i) {
                auto& ps = gs.players[i];
                auto& cp = g_players[i];
                cp.x     = static_cast<float>(static_cast<int32_t>(ntohl(ps.x))) / FP_SCALE;
                cp.y     = static_cast<float>(static_cast<int32_t>(ntohl(ps.y))) / FP_SCALE;
                cp.angle = static_cast<float>(static_cast<int16_t>(ntohs(ps.angle))) / 10.0f;
                cp.score = ntohs(ps.score);
                cp.alive = ps.alive;
                cp.lives = ps.lives;
                strncpy_s(cp.name, sizeof(cp.name), ps.name, _TRUNCATE);
                cp.active = true;
            }
            for (int i = gs.playerCount; i < MAX_PLAYERS; ++i) g_players[i].active = false;

            for (int i = 0; i < gs.asteroidCount; ++i) {
                auto& as = gs.asteroids[i];
                auto& ca = g_asteroids[i];
                ca.id    = as.id;
                ca.x     = static_cast<float>(static_cast<int32_t>(ntohl(as.x))) / FP_SCALE;
                ca.y     = static_cast<float>(static_cast<int32_t>(ntohl(as.y))) / FP_SCALE;
                ca.size  = as.size;
                ca.alive = as.alive != 0;
            }
            for (int i = gs.asteroidCount; i < MAX_ASTEROIDS; ++i) g_asteroids[i].alive = false;

            for (int i = 0; i < gs.bulletCount; ++i) {
                auto& bs = gs.bullets[i];
                auto& cb = g_bullets[i];
                cb.ownerId = bs.ownerId;
                cb.x       = static_cast<float>(static_cast<int32_t>(ntohl(bs.x))) / FP_SCALE;
                cb.y       = static_cast<float>(static_cast<int32_t>(ntohl(bs.y))) / FP_SCALE;
                cb.alive   = bs.alive != 0;
            }
            for (int i = gs.bulletCount; i < MAX_BULLETS; ++i) g_bullets[i].alive = false;
            break;
        }

        case MSG_SCORE_UPDATE: {
            PktScoreUpdate su;
            std::memcpy(&su, buf, sizeof(PktScoreUpdate));
            std::lock_guard<std::mutex> lock(g_stateMtx);
            g_scoreCount = su.count;
            for (int i = 0; i < su.count; ++i) {
                g_scores[i].id    = su.entries[i].playerId;
                g_scores[i].score = ntohs(su.entries[i].score);
                strncpy_s(g_scores[i].name, sizeof(g_scores[i].name), su.entries[i].name, _TRUNCATE);
            }
            break;
        }

        case MSG_ASTEROID_DEAD: {
            PktAsteroidDead ad;
            std::memcpy(&ad, buf, sizeof(PktAsteroidDead));
            std::lock_guard<std::mutex> lock(g_stateMtx);
            for (auto& a : g_asteroids) {
                if (a.id == ad.asteroidId) { a.alive = false; break; }
            }
            // ACK is sent at top of loop via seq != SEQ_UNRELIABLE check
            break;
        }

        case MSG_BULLET_SPAWN: {
            PktBulletSpawn bs;
            std::memcpy(&bs, buf, sizeof(PktBulletSpawn));
            std::lock_guard<std::mutex> lock(g_stateMtx);
            // Find or add bullet slot
            for (auto& cb : g_bullets) {
                if (!cb.alive) {
                    cb.ownerId = bs.bullet.ownerId;
                    cb.x = static_cast<float>(static_cast<int32_t>(ntohl(bs.bullet.x))) / FP_SCALE;
                    cb.y = static_cast<float>(static_cast<int32_t>(ntohl(bs.bullet.y))) / FP_SCALE;
                    cb.alive = true;
                    break;
                }
            }
            break;
        }

        case MSG_PLAYER_DEAD: {
            PktPlayerDead pd;
            std::memcpy(&pd, buf, sizeof(pd));
            std::lock_guard<std::mutex> lock(g_stateMtx);
            for (int i = 0; i < g_playerCount; ++i) {
                // Mark dead in local state until next game state arrives
                // (will be overwritten by next MSG_GAME_STATE)
                (void)pd; // lives updated via game state
            }
            break;
        }

        case MSG_PLAYER_JOIN: {
            PktPlayerEvent pe;
            std::memcpy(&pe, buf, sizeof(pe));
            std::lock_guard<std::mutex> cLock(g_chatMtx);
            char msg[64];
            std::snprintf(msg, sizeof(msg), "*** %s joined the game ***", pe.name);
            g_chatLog.push_back(msg);
            break;
        }

        case MSG_PLAYER_LEAVE: {
            PktPlayerEvent pe;
            std::memcpy(&pe, buf, sizeof(pe));
            std::lock_guard<std::mutex> cLock(g_chatMtx);
            char msg[64];
            std::snprintf(msg, sizeof(msg), "*** %s left the game ***", pe.name);
            g_chatLog.push_back(msg);
            break;
        }

        case MSG_CHAT_BROADCAST: {
            PktChat chat;
            std::memcpy(&chat, buf, sizeof(chat));
            std::lock_guard<std::mutex> cLock(g_chatMtx);
            g_chatLog.push_back(std::string(chat.text));
            if (g_chatLog.size() > 20) g_chatLog.erase(g_chatLog.begin());
            break;
        }

        case MSG_ROUND_OVER: {
            PktRoundOver ro;
            std::memcpy(&ro, buf, sizeof(ro));
            std::lock_guard<std::mutex> lock(g_stateMtx);
            g_roundOver = true;
            g_resultLines.clear();
            g_resultLines.push_back("--- Final Scores ---");
            for (int i = 0; i < ro.winnerCount; ++i) {
                char line[64];
                std::snprintf(line, sizeof(line), "%d. %-15s %u pts",
                              i + 1, ro.winners[i].name, ntohs(ro.winners[i].score));
                g_resultLines.push_back(line);
            }
            g_resultLines.push_back("");
            g_resultLines.push_back("--- All-Time Top 5 ---");
            for (int i = 0; i < ro.leaderboardCount; ++i) {
                char line[80];
                std::snprintf(line, sizeof(line), "%d. %-15s %5u  %s",
                              i + 1,
                              ro.leaderboard[i].name,
                              ro.leaderboard[i].score,
                              ro.leaderboard[i].datetime);
                g_resultLines.push_back(line);
            }
            break;
        }

        case MSG_PONG: {
            using Clock = std::chrono::steady_clock;
            PktPing pong;
            std::memcpy(&pong, buf, sizeof(pong));
            uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                               Clock::now().time_since_epoch()).count();
            uint64_t sent = pong.clientTime; // echoed back verbatim
            std::lock_guard<std::mutex> lock(g_stateMtx);
            g_pingMs = static_cast<float>(now - sent) / 1000.0f;
            break;
        }

        default: break;
        }
    }
}

// ── Input thread ──────────────────────────────────────────────────────────────
// Reads key state and sends input packets to the server.
static void InputThread() {
    using Clock = std::chrono::steady_clock;
    auto nextSend = Clock::now();
    constexpr int INPUT_HZ = 30;
    auto sendInterval = std::chrono::milliseconds(1000 / INPUT_HZ);

    // Key state
    bool kLeft  = false, kRight = false, kThrust = false, kFire = false;
    bool chatMode = false;
    std::string chatBuf;

    while (g_running) {
        // Poll keyboard (non-blocking)
#ifdef _WIN32
        if (_kbhit()) {
            int k = _getch();
            if (chatMode) {
                if (k == '\r' || k == '\n') {
                    // Send chat
                    if (!chatBuf.empty()) {
                        PktChat pkt{};
                        pkt.hdr.type = MSG_CHAT;
                        pkt.hdr.playerId = g_myId;
                        pkt.hdr.seq = htons(SEQ_UNRELIABLE);
                        strncpy_s(pkt.text, sizeof(pkt.text), chatBuf.c_str(), _TRUNCATE);
                        SendTo(&pkt, sizeof(pkt));
                        chatBuf.clear();
                    }
                    chatMode = false;
                } else if (k == 27) { // ESC
                    chatMode = false;
                    chatBuf.clear();
                } else if (k == '\b' && !chatBuf.empty()) {
                    chatBuf.pop_back();
                } else if (k >= 32 && k < 127) {
                    chatBuf += static_cast<char>(k);
                }
            } else {
                if (k == 'q' || k == 'Q') { SendQuit(); g_running = false; }
                if (k == 't' || k == 'T') { chatMode = true; chatBuf.clear(); }
                // Arrow keys come as 0xE0 + code on Windows
                if (k == 0xE0 || k == 0x00) {
                    int k2 = _getch();
                    if (k2 == 75) kLeft   = true;  // left arrow
                    if (k2 == 77) kRight  = true;  // right arrow
                    if (k2 == 72) kThrust = true;  // up arrow
                } else {
                    if (k == 'a' || k == 'A') kLeft   = true;
                    if (k == 'd' || k == 'D') kRight  = true;
                    if (k == 'w' || k == 'W') kThrust = true;
                    if (k == ' ')             kFire   = true;
                }
            }
        } else {
            // Reset one-shot keys each poll cycle when not held
            // For simplicity we reset all on each cycle then re-poll
            kLeft = kRight = kThrust = kFire = false;
            if (GetAsyncKeyState(VK_LEFT)  & 0x8000) kLeft   = true;
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) kRight  = true;
            if (GetAsyncKeyState(VK_UP)    & 0x8000) kThrust = true;
            if (GetAsyncKeyState(VK_SPACE) & 0x8000) kFire   = true;
            if (GetAsyncKeyState('A')      & 0x8000) kLeft   = true;
            if (GetAsyncKeyState('D')      & 0x8000) kRight  = true;
            if (GetAsyncKeyState('W')      & 0x8000) kThrust = true;
        }
#endif
        // Build and send input packet
        auto now = Clock::now();
        if (now >= nextSend && !chatMode) {
            nextSend = now + sendInterval;
            uint8_t keys = 0;
            if (kLeft)   keys |= INPUT_ROTATE_LEFT;
            if (kRight)  keys |= INPUT_ROTATE_RIGHT;
            if (kThrust) keys |= INPUT_THRUST;
            if (kFire)   keys |= INPUT_FIRE;
            if (g_joined) SendInput(keys);
            ++g_localTick;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ── Ping thread ───────────────────────────────────────────────────────────────
static void PingThread() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (g_joined) SendPing();
    }
}

// ── Reconnect logic ───────────────────────────────────────────────────────────
static void ReconnectLoop() {
    // If we haven't heard from server in 3s, re-send JOIN
    using Clock = std::chrono::steady_clock;
    auto lastJoin = Clock::now();
    uint32_t lastKnownTick = 0;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        {
            std::lock_guard<std::mutex> lock(g_stateMtx);
            if (g_lastServerTick != lastKnownTick) {
                lastKnownTick = g_lastServerTick;
                lastJoin = Clock::now();
                continue;
            }
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           Clock::now() - lastJoin).count();
        if (elapsed >= 3) {
            std::cout << "[Client] No server response, re-sending JOIN...\n";
            SendJoin();
            lastJoin = Clock::now();
        }
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    LoadConfig("config.txt");

    std::cout << "=== Spaceships Client ===\n";
    std::cout << "Server IP   [" << g_serverIP << "]: ";
    std::string line;
    std::getline(std::cin, line);
    if (!line.empty()) g_serverIP = line;

    std::cout << "Server Port [" << g_serverPort << "]: ";
    std::getline(std::cin, line);
    if (!line.empty()) g_serverPort = static_cast<uint16_t>(std::stoi(line));

    std::cout << "Client Port [" << g_clientPort << "]: ";
    std::getline(std::cin, line);
    if (!line.empty()) g_clientPort = static_cast<uint16_t>(std::stoi(line));

    std::cout << "Player Name [" << g_playerName << "]: ";
    std::getline(std::cin, line);
    if (!line.empty()) strncpy_s(g_playerName, sizeof(g_playerName), line.c_str(), _TRUNCATE);

    if (!InitNetworking()) {
        std::cerr << "[Client] Failed to initialise networking.\n";
        return 1;
    }

    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
        std::cerr << "[Client] socket() failed.\n";
        CleanupNetworking();
        return 1;
    }

    // Bind to client port
    sockaddr_in bindAddr{};
    bindAddr.sin_family      = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port        = htons(g_clientPort);
    if (bind(g_sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) == SOCKET_ERROR) {
        // Try a random port if specified one is busy
        bindAddr.sin_port = 0;
        bind(g_sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
    }

    // Set recv timeout
#ifdef _WIN32
    DWORD tv = 100;
#else
    struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 100000;
#endif
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));

    // Setup server address
    g_serverAddr.sin_family = AF_INET;
    g_serverAddr.sin_port   = htons(g_serverPort);
    inet_pton(AF_INET, g_serverIP.c_str(), &g_serverAddr.sin_addr);

    std::cout << "[Client] Connecting to " << g_serverIP << ":" << g_serverPort << "...\n";
    SendJoin();

    // Start threads
    std::thread recvThr(RecvThread);
    std::thread inputThr(InputThread);
    std::thread pingThr(PingThread);
    std::thread reconnThr(ReconnectLoop);
    std::thread renderThr(RenderLoop);

    // Wait for exit signal (input thread sets g_running = false on Q)
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    g_running = false;

    if (recvThr.joinable())   recvThr.join();
    if (inputThr.joinable())  inputThr.join();
    if (pingThr.joinable())   pingThr.join();
    if (reconnThr.joinable()) reconnThr.join();
    if (renderThr.joinable()) renderThr.join();

    closesocket(g_sock);
    CleanupNetworking();
    std::cout << "[Client] Exiting.\n";
    return 0;
}
