/* Start Header ***************************************************************/
/*!
\file server_main.cpp  (Spaceships game server – Assignment 5)
\author TODO
\par email: TODO
\date 27 Mar, 2026
\brief
  Authoritative game server for the Spaceships multiplayer LAN game.
  - Pure UDP (required for options 1/2).
  - Client-Server architecture.
  - Handles up to MAX_PLAYERS clients simultaneously.
  - Lock-step ACK for critical events (asteroid destruction, round-over).
  - Persists top-5 leaderboard to disk.
  - Graceful disconnect: other players continue when one drops.
  - Reconnect support: rejoining client receives full state.
*/
/* End Header *****************************************************************/

#define NOMINMAX
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
#include <ctime>
#include <algorithm>
#include <random>
#include <cstdio>

// ── Configuration (loaded from server_config.txt) ────────────────────────────
static uint16_t g_serverPort = GAME_PORT;
static int      g_tickRateHz = 30;
static int      g_minPlayers = 2;
static int      g_maxPlayers = MAX_PLAYERS;
static std::string g_leaderboardFile = "leaderboard.bin";

// ── Server state ─────────────────────────────────────────────────────────────
enum class GamePhase { WAITING, PLAYING, ROUND_OVER };

struct ServerPlayer {
    sockaddr_in addr{};
    char        name[16]{};
    uint8_t     id = 0;
    bool        connected = false;
    bool        alive = true;
    uint8_t     health = PLAYER_MAX_HEALTH;
    int32_t     x = 0, y = 0;
    int16_t     angle = 0;
    int32_t     velX = 0, velY = 0;
    uint16_t    score = 0;
    uint8_t     lives = INITIAL_LIVES;
    uint32_t    respawnTick = 0;
    uint8_t     keys = 0;
    uint32_t    lastSeenTick = 0;
    bool        ackedAsteroidDead = false;
    bool        ackedRoundOver = false;
};

struct ServerAsteroid {
    uint8_t  id = 0;
    int32_t  x = 0, y = 0;
    int32_t  velX = 0, velY = 0;
    uint8_t  size = 0;
    bool     alive = false;
};

struct ServerBullet {
    uint8_t  id = 0;
    uint8_t  ownerId = 0;
    int32_t  x = 0, y = 0;
    int32_t  velX = 0, velY = 0;
    uint32_t birthTick = 0;
    bool     alive = false;
};

static SOCKET g_sock = INVALID_SOCKET;
static std::mutex g_mtx;
static std::array<ServerPlayer, MAX_PLAYERS> g_players{};
static std::array<ServerAsteroid, MAX_ASTEROIDS> g_asteroids{};
static std::array<ServerBullet, MAX_BULLETS> g_bullets{};
static GamePhase g_phase = GamePhase::WAITING;
static uint32_t g_tick = 0;
static uint16_t g_seqCounter = 1;
static std::atomic<bool> g_running{ true };
static std::mt19937 g_rng{ std::random_device{}() };

// Pending reliable messages that need ACK from all players
struct ReliableMsg {
    uint16_t seq{};
    MsgType  type{};
    std::vector<uint8_t> data;
    uint32_t sentTick{};
    std::array<bool, MAX_PLAYERS> ackedBy{};
};
static std::vector<ReliableMsg> g_pendingAcks;

// Leaderboard
static std::array<LeaderboardEntry, LEADERBOARD_SIZE> g_leaderboard{};
static int g_leaderboardCount = 0;

// ── Utility ──────────────────────────────────────────────────────────────────
static uint16_t NextSeq() { return g_seqCounter++; }

static void SendTo(const void* data, int len, const sockaddr_in& addr) {
    sendto(g_sock, reinterpret_cast<const char*>(data), len, 0,
        reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
}

static void Broadcast(const void* data, int len) {
    for (auto& p : g_players) {
        if (p.connected) {
            SendTo(data, len, p.addr);
        }
    }
}

static bool AddrEqual(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

static ServerPlayer* FindPlayer(const sockaddr_in& addr) {
    for (auto& p : g_players) {
        if (p.connected && AddrEqual(p.addr, addr)) return &p;
    }
    return nullptr;
}

static int CountConnected() {
    int n = 0;
    for (auto& p : g_players) {
        if (p.connected) ++n;
    }
    return n;
}

static int CountAlive() {
    int n = 0;
    for (auto& p : g_players) {
        if (p.connected && p.alive) ++n;
    }
    return n;
}

// ── Config loading ────────────────────────────────────────────────────────────
static void LoadConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "[Server] No config file found, using defaults.\n";
        return;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (!val.empty() && val.back() == '\r') val.pop_back();

        if (key == "SERVER_PORT") g_serverPort = static_cast<uint16_t>(std::stoi(val));
        if (key == "TICK_RATE") g_tickRateHz = std::stoi(val);
        if (key == "MIN_PLAYERS") g_minPlayers = std::stoi(val);
        if (key == "MAX_PLAYERS")    g_maxPlayers = (std::min)(std::stoi(val), MAX_PLAYERS);
        if (key == "LEADERBOARD") g_leaderboardFile = val;
    }
}

// ── Leaderboard ──────────────────────────────────────────────────────────────
static void LoadLeaderboard() {
    std::ifstream f(g_leaderboardFile, std::ios::binary);
    if (!f.is_open()) {
        g_leaderboardCount = 0;
        return;
    }

    f.read(reinterpret_cast<char*>(g_leaderboard.data()),
        LEADERBOARD_SIZE * sizeof(LeaderboardEntry));
    g_leaderboardCount = static_cast<int>(f.gcount()) / sizeof(LeaderboardEntry);
}

static void SaveLeaderboard() {
    std::ofstream f(g_leaderboardFile, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return;

    f.write(reinterpret_cast<const char*>(g_leaderboard.data()),
        g_leaderboardCount * sizeof(LeaderboardEntry));
}

static void UpdateLeaderboard(const char* playerName, uint16_t score) {
    time_t now = time(nullptr);
    char timebuf[20]{};

    tm tmInfo{};
    localtime_s(&tmInfo, &now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tmInfo);

    int insertAt = g_leaderboardCount;
    for (int i = 0; i < g_leaderboardCount; ++i) {
        if (score > g_leaderboard[i].score) {
            insertAt = i;
            break;
        }
    }

    if (insertAt >= LEADERBOARD_SIZE) return;

    for (int i = std::min(g_leaderboardCount, LEADERBOARD_SIZE - 1); i > insertAt; --i) {
        g_leaderboard[i] = g_leaderboard[i - 1];
    }

    std::memset(&g_leaderboard[insertAt], 0, sizeof(LeaderboardEntry));
    strncpy_s(g_leaderboard[insertAt].name,
        sizeof(g_leaderboard[insertAt].name),
        playerName,
        _TRUNCATE);

    g_leaderboard[insertAt].score = score;

    strncpy_s(g_leaderboard[insertAt].datetime,
        sizeof(g_leaderboard[insertAt].datetime),
        timebuf,
        _TRUNCATE);

    if (g_leaderboardCount < LEADERBOARD_SIZE) {
        ++g_leaderboardCount;
    }
}

// ── Asteroid helpers ──────────────────────────────────────────────────────────
static uint8_t NextAsteroidId() {
    for (uint8_t i = 1; i <= MAX_ASTEROIDS; ++i) {
        bool used = false;
        for (auto& a : g_asteroids) {
            if (a.alive && a.id == i) {
                used = true;
                break;
            }
        }
        if (!used) return i;
    }
    return 0;
}

static uint8_t NextBulletId() {
    for (uint8_t i = 1; i <= MAX_BULLETS; ++i) {
        bool used = false;
        for (auto& b : g_bullets) {
            if (b.alive && b.id == i) {
                used = true;
                break;
            }
        }
        if (!used) return i;
    }
    return 0;
}

static ServerAsteroid* GetFreeAsteroidSlot() {
    for (auto& a : g_asteroids) {
        if (!a.alive) return &a;
    }
    return nullptr;
}

static ServerBullet* GetFreeBulletSlot() {
    for (auto& b : g_bullets) {
        if (!b.alive) return &b;
    }
    return nullptr;
}

static void SpawnAsteroid(uint8_t size, int32_t x, int32_t y, int32_t vx, int32_t vy) {
    ServerAsteroid* slot = GetFreeAsteroidSlot();
    if (!slot) return;

    uint8_t id = NextAsteroidId();
    if (!id) return;

    slot->id = id;
    slot->size = size;
    slot->x = x;
    slot->y = y;
    slot->velX = vx;
    slot->velY = vy;
    slot->alive = true;

    PktAsteroidSpawn pkt{};
    pkt.hdr.type = MSG_ASTEROID_SPAWN;
    pkt.hdr.playerId = 0;
    pkt.hdr.seq = htons(NextSeq());
    pkt.hdr.timestamp = htonl(g_tick);
    pkt.ast.id = id;
    pkt.ast.size = size;
    pkt.ast.alive = 1;
    pkt.ast.x = htonl(x);
    pkt.ast.y = htonl(y);
    pkt.ast.velX = htons(static_cast<uint16_t>(vx));
    pkt.ast.velY = htons(static_cast<uint16_t>(vy));
    Broadcast(&pkt, sizeof(pkt));
}

static void SpawnInitialAsteroids() {
}

static void SetSpawnPosition(ServerPlayer& p) {
    const int32_t groundYFP         = 180 * FP_SCALE;
    const int32_t platformTopYFP    = 420 * FP_SCALE;
    const int32_t platformTopLMinFP = 100 * FP_SCALE;
    const int32_t platformTopLMaxFP = 300 * FP_SCALE;
    const int32_t platformTopRMinFP = 500 * FP_SCALE;
    const int32_t platformTopRMaxFP = 700 * FP_SCALE;

    const int32_t leftGroundXFP  = 80 * FP_SCALE;
    const int32_t rightGroundXFP = (WORLD_W - 80) * FP_SCALE;

    int32_t x = (WORLD_W / 2) * FP_SCALE;
    int32_t y = groundYFP;

    switch (p.id) {
    case 1:
        x = leftGroundXFP;
        y = groundYFP;
        break;
    case 2:
        x = rightGroundXFP;
        y = groundYFP;
        break;
    case 3:
        x = (platformTopLMinFP + platformTopLMaxFP) / 2;
        y = platformTopYFP;
        break;
    case 4:
        x = (platformTopRMinFP + platformTopRMaxFP) / 2;
        y = platformTopYFP;
        break;
    default:
        break;
    }

    p.x = x;
    p.y = y;
    p.velX = 0;
    p.velY = 0;
}

// ── Reliable message helpers ──────────────────────────────────────────────────
static void SendReliable(MsgType type, const void* data, int len) {
    ReliableMsg rm{};
    rm.seq = ntohs(reinterpret_cast<const PktHeader*>(data)->seq);
    rm.type = type;
    rm.data.assign(reinterpret_cast<const uint8_t*>(data),
        reinterpret_cast<const uint8_t*>(data) + len);
    rm.sentTick = g_tick;
    rm.ackedBy.fill(false);
    g_pendingAcks.push_back(std::move(rm));
    Broadcast(data, len);
}

static void ProcessAck(const ServerPlayer& player, uint16_t ackSeq) {
    for (auto& rm : g_pendingAcks) {
        if (rm.seq == ackSeq) {
            rm.ackedBy[player.id - 1] = true;
            break;
        }
    }
}

static bool AllAcked(const ReliableMsg& rm) {
    for (auto& p : g_players) {
        if (p.connected && !rm.ackedBy[p.id - 1]) return false;
    }
    return true;
}

static void RetransmitPending() {
    constexpr uint32_t RETRY_INTERVAL = 15;
    for (auto& rm : g_pendingAcks) {
        if (g_tick - rm.sentTick >= RETRY_INTERVAL) {
            rm.sentTick = g_tick;
            for (auto& p : g_players) {
                if (p.connected && !rm.ackedBy[p.id - 1]) {
                    SendTo(rm.data.data(), static_cast<int>(rm.data.size()), p.addr);
                }
            }
        }
    }
}

// ── Score broadcast ───────────────────────────────────────────────────────────
static void BroadcastScores() {
    PktScoreUpdate pkt{};
    pkt.hdr.type = MSG_SCORE_UPDATE;
    pkt.hdr.playerId = 0;
    pkt.hdr.seq = htons(SEQ_UNRELIABLE);
    pkt.hdr.timestamp = htonl(g_tick);

    int idx = 0;
    for (auto& p : g_players) {
        if (p.connected) {
            pkt.entries[idx].playerId = p.id;
            pkt.entries[idx].score = htons(p.score);
            strncpy_s(pkt.entries[idx].name, sizeof(pkt.entries[idx].name), p.name, _TRUNCATE);
            ++idx;
        }
    }

    pkt.count = static_cast<uint8_t>(idx);
    Broadcast(&pkt, sizeof(pkt));
}

// ── Round over ────────────────────────────────────────────────────────────────
static void EndRound() {
    g_phase = GamePhase::ROUND_OVER;
    std::cout << "[Server] Round over!\n";

    std::vector<ServerPlayer*> sorted;
    for (auto& p : g_players) {
        if (p.connected) sorted.push_back(&p);
    }

    std::sort(sorted.begin(), sorted.end(),
        [](const ServerPlayer* a, const ServerPlayer* b) {
            return a->score > b->score;
        });

    for (auto* p : sorted) UpdateLeaderboard(p->name, p->score);
    SaveLeaderboard();

    PktRoundOver pkt{};
    pkt.hdr.type = MSG_ROUND_OVER;
    pkt.hdr.playerId = 0;
    uint16_t seq = NextSeq();
    pkt.hdr.seq = htons(seq);
    pkt.hdr.timestamp = htonl(g_tick);

    int w = 0;
    for (auto* p : sorted) {
        pkt.winners[w].playerId = p->id;
        pkt.winners[w].score = htons(p->score);
        strncpy_s(pkt.winners[w].name, sizeof(pkt.winners[w].name), p->name, _TRUNCATE);
        ++w;
    }
    pkt.winnerCount = static_cast<uint8_t>(w);

    pkt.leaderboardCount = static_cast<uint8_t>(g_leaderboardCount);
    for (int i = 0; i < g_leaderboardCount; ++i) {
        pkt.leaderboard[i] = g_leaderboard[i];
    }

    SendReliable(MSG_ROUND_OVER, &pkt, sizeof(pkt));
}

// ── Bullet-player collision ───────────────────────────────────────────────────
static void CheckBulletPlayerCollisions() {
    for (auto& bullet : g_bullets) {
        if (!bullet.alive) continue;

        for (auto& p : g_players) {
            if (!p.connected || !p.alive) continue;
            if (p.id == bullet.ownerId) continue;

            int32_t dx = bullet.x - p.x;
            if (dx < 0) dx = -dx;
            int32_t dy = bullet.y - p.y;
            if (dy < 0) dy = -dy;

            if (dx <= (FIGHTER_HALF_WIDTH_FP + BULLET_RADIUS_FP) &&
                dy <= (FIGHTER_HALF_HEIGHT_FP + BULLET_RADIUS_FP)) {
                bullet.alive = false;

                if (p.health > 0) {
                    if (p.health > BULLET_DAMAGE_HP)
                        p.health = static_cast<uint8_t>(p.health - BULLET_DAMAGE_HP);
                    else
                        p.health = 0;
                }

                if (p.health == 0) {
                    p.alive = false;
                    if (p.lives > 0) --p.lives;
                    p.respawnTick = g_tick + RESPAWN_TICKS;

                    PktPlayerDead pkt{};
                    pkt.hdr.type = MSG_PLAYER_DEAD;
                    pkt.hdr.playerId = 0;
                    pkt.hdr.seq = htons(SEQ_UNRELIABLE);
                    pkt.hdr.timestamp = htonl(g_tick);
                    pkt.deadPlayerId = p.id;
                    pkt.livesRemaining = p.lives;
                    Broadcast(&pkt, sizeof(pkt));

                    if (g_phase == GamePhase::PLAYING)
                    {
                        for (auto& k : g_players) {
                            if (k.connected && k.id == bullet.ownerId) {
                                k.score = static_cast<uint16_t>(k.score + SCORE_PLAYER_KILL);
                                break;
                            }
                        }

                        int aliveWithLives = 0;
                        for (auto& q : g_players) {
                            if (q.connected && q.lives > 0) {
                                ++aliveWithLives;
                            }
                        }

                        if (aliveWithLives <= 1) {
                            EndRound();
                        }
                    }

                }

                break;
            }
        }
    }
}

static void CheckBulletAsteroidCollisions() {
}

static void CheckShipAsteroidCollisions() {
}

// ── Game state broadcast ──────────────────────────────────────────────────────
static void BroadcastGameState() {
    PktGameState pkt{};
    pkt.hdr.type = MSG_GAME_STATE;
    pkt.hdr.playerId = 0;
    pkt.hdr.seq = htons(SEQ_UNRELIABLE);
    pkt.hdr.timestamp = htonl(g_tick);
    pkt.gameTick = htonl(g_tick);

    int pi = 0;
    for (auto& p : g_players) {
        if (!p.connected) continue;
        auto& ps = pkt.players[pi++];
        ps.playerId = p.id;
        ps.x = htonl(p.x);
        ps.y = htonl(p.y);
        ps.angle = htons(static_cast<uint16_t>(p.angle));
        ps.velX = htons(static_cast<uint16_t>(p.velX));
        ps.velY = htons(static_cast<uint16_t>(p.velY));
        ps.score = htons(p.score);
        ps.alive = p.alive ? 1 : 0;
        ps.lives = p.lives;
        ps.health = p.health;
        strncpy_s(ps.name, sizeof(ps.name), p.name, _TRUNCATE);
    }
    pkt.playerCount = static_cast<uint8_t>(pi);

    pkt.asteroidCount = 0;

    int bi = 0;
    for (auto& b : g_bullets) {
        if (!b.alive) continue;
        auto& bs = pkt.bullets[bi++];
        bs.id = b.id;
        bs.ownerId = b.ownerId;
        bs.alive = 1;
        bs.x = htonl(b.x);
        bs.y = htonl(b.y);
        bs.velX = htons(static_cast<uint16_t>(b.velX));
        bs.velY = htons(static_cast<uint16_t>(b.velY));
    }
    pkt.bulletCount = static_cast<uint8_t>(bi);

    Broadcast(&pkt, sizeof(pkt));
}

// ── Join handling ─────────────────────────────────────────────────────────────
static void StartGameFromWaiting() {
    if (g_phase != GamePhase::WAITING) return;
    int connected = CountConnected();
    if (connected < g_minPlayers) return;
    std::cout << "[Server] Starting game with " << connected << " players.\n";
    g_phase = GamePhase::PLAYING;
    for (auto& p : g_players) {
        if (!p.connected) continue;
        p.alive  = true;
        p.health = PLAYER_MAX_HEALTH;
        p.lives  = INITIAL_LIVES;
        SetSpawnPosition(p);
    }
    BroadcastGameState();
}

static void HandleJoin(const PktJoin& pkt, const sockaddr_in& from) {
    for (auto& p : g_players) {
        if (p.connected && AddrEqual(p.addr, from)) {
            PktJoinAck ack{};
            ack.hdr.type = MSG_REJOIN_ACK;
            ack.hdr.playerId = 0;
            ack.hdr.seq = htons(SEQ_UNRELIABLE);
            ack.assignedId = p.id;
            ack.currentPlayerCount = static_cast<uint8_t>(CountConnected());
            ack.tickRate = htons(static_cast<uint16_t>(g_tickRateHz));
            SendTo(&ack, sizeof(ack), from);
            BroadcastGameState();
            return;
        }
    }

    if (CountConnected() >= g_maxPlayers || g_phase == GamePhase::ROUND_OVER) {
        PktHeader deny{};
        deny.type = MSG_JOIN_DENY;
        SendTo(&deny, sizeof(deny), from);
        return;
    }

    ServerPlayer* slot = nullptr;
    uint8_t newId = 1;
    for (auto& p : g_players) {
        if (!p.connected) {
            slot = &p;
            break;
        }
        ++newId;
    }

    if (!slot) {
        PktHeader deny{};
        deny.type = MSG_JOIN_DENY;
        SendTo(&deny, sizeof(deny), from);
        return;
    }

    int count = CountConnected();
    slot->id = newId;
    slot->addr = from;

    const char* color = "White";
    switch (newId) {
        case 1: color = "Red"; break;
        case 2: color = "Green"; break;
        case 3: color = "Blue"; break;
        case 4: color = "White"; break;
        case 5: color = "Cyan"; break;
        case 6: color = "Magenta"; break;
        default: break;
    }

    if (pkt.name[0] == '\0' || strcmp(pkt.name, "Player") == 0) {
        std::snprintf(slot->name, sizeof(slot->name), "Player %s", color);
    }
    else {
        strncpy_s(slot->name, sizeof(slot->name), pkt.name, _TRUNCATE);
    }

    slot->connected = true;
    slot->alive = (g_phase == GamePhase::PLAYING);
    slot->health = PLAYER_MAX_HEALTH;
    slot->lives = INITIAL_LIVES;
    slot->score = 0;
    SetSpawnPosition(*slot);
    slot->angle = 0;
    slot->lastSeenTick = g_tick;

    std::cout << "[Server] Player " << static_cast<int>(newId) << " joined: " << slot->name << "\n";

    PktJoinAck ack{};
    ack.hdr.type = MSG_JOIN_ACK;
    ack.hdr.playerId = 0;
    ack.hdr.seq = htons(SEQ_UNRELIABLE);
    ack.assignedId = newId;
    ack.currentPlayerCount = static_cast<uint8_t>(CountConnected());
    ack.tickRate = htons(static_cast<uint16_t>(g_tickRateHz));

    int ni = 0;
    for (auto& p : g_players) {
        if (p.connected) {
            strncpy_s(ack.names[ni], sizeof(ack.names[ni]), p.name, _TRUNCATE);
            ++ni;
        }
    }

    SendTo(&ack, sizeof(ack), from);

    PktPlayerEvent evt{};
    evt.hdr.type = MSG_PLAYER_JOIN;
    evt.hdr.seq = htons(SEQ_UNRELIABLE);
    evt.eventPlayerId = newId;
    strncpy_s(evt.name, sizeof(evt.name), slot->name, _TRUNCATE);
    Broadcast(&evt, sizeof(evt));
}

static void HandleStartRequest(const PktStartRequest& pkt, const sockaddr_in& from) {
    ServerPlayer* p = FindPlayer(from);
    if (!p) return;
    if (g_phase != GamePhase::WAITING) return;
    if (p->id != 1) return;
    StartGameFromWaiting();
}

// ── Input handling ────────────────────────────────────────────────────────────
static void HandleInput(const PktInput& pkt, const sockaddr_in& from) {
    ServerPlayer* p = FindPlayer(from);
    if (!p) return;
    p->keys = pkt.keys;
    p->angle = ntohs(pkt.aimAngle);
    p->lastSeenTick = g_tick;
}

// ── Chat handling ─────────────────────────────────────────────────────────────
static void HandleChat(const PktChat& pkt, const sockaddr_in& from) {
    ServerPlayer* p = FindPlayer(from);
    if (!p) return;

    PktChat relay{};
    relay.hdr.type = MSG_CHAT_BROADCAST;
    relay.hdr.playerId = p->id;
    relay.hdr.seq = htons(SEQ_UNRELIABLE);
    std::snprintf(relay.text, sizeof(relay.text), "[%s] %s", p->name, pkt.text);
    Broadcast(&relay, sizeof(relay));
}

// ── Quit handling ─────────────────────────────────────────────────────────────
static void HandleQuit(const sockaddr_in& from) {
    ServerPlayer* p = FindPlayer(from);
    if (!p) return;

    std::cout << "[Server] Player " << static_cast<int>(p->id) << " (" << p->name << ") quit.\n";

    PktPlayerEvent evt{};
    evt.hdr.type = MSG_PLAYER_LEAVE;
    evt.hdr.seq = htons(SEQ_UNRELIABLE);
    evt.eventPlayerId = p->id;
    strncpy_s(evt.name, sizeof(evt.name), p->name, _TRUNCATE);

    p->connected = false;
    Broadcast(&evt, sizeof(evt));

    if (g_phase == GamePhase::PLAYING &&
        CountConnected() < g_minPlayers &&
        CountConnected() > 0) {
        EndRound();
    }
}

// ── Receive thread ────────────────────────────────────────────────────────────
static void RecvThread() {
    char buf[4096];
    sockaddr_in from{};

    while (g_running) {
        int fromLen = sizeof(from);
        int r = recvfrom(g_sock, buf, sizeof(buf), 0,
            reinterpret_cast<sockaddr*>(&from), &fromLen);

        if (r < static_cast<int>(sizeof(PktHeader))) continue;

        PktHeader hdr{};
        std::memcpy(&hdr, buf, sizeof(PktHeader));

        std::lock_guard<std::mutex> lock(g_mtx);

        switch (static_cast<MsgType>(hdr.type)) {
        case MSG_JOIN:
            HandleJoin(*reinterpret_cast<PktJoin*>(buf), from);
            break;
        case MSG_INPUT:
            HandleInput(*reinterpret_cast<PktInput*>(buf), from);
            break;
        case MSG_CHAT:
            HandleChat(*reinterpret_cast<PktChat*>(buf), from);
            break;
        case MSG_QUIT:
            HandleQuit(from);
            break;
        case MSG_START_REQUEST:
            HandleStartRequest(*reinterpret_cast<PktStartRequest*>(buf), from);
            break;
        case MSG_ACK: {
            PktAck ack{};
            std::memcpy(&ack, buf, sizeof(PktAck));
            ServerPlayer* p = FindPlayer(from);
            if (p) ProcessAck(*p, ntohs(ack.ackSeq));
            break;
        }
        case MSG_PING:
            buf[0] = static_cast<char>(MSG_PONG);
            SendTo(buf, r, from);
            break;
        default:
            break;
        }
    }
}

// ── Physics update (server-authoritative) ────────────────────────────────────
static void UpdatePhysics() {
    const int32_t worldWidthFP    = WORLD_W * FP_SCALE;
    const int32_t groundYFP       = 180 * FP_SCALE;
    const int32_t groundXMinFP    = 40  * FP_SCALE;
    const int32_t groundXMaxFP    = 760 * FP_SCALE;
    const int32_t platformMidYFP  = 270 * FP_SCALE;
    const int32_t platformMidMinFP = 200 * FP_SCALE;
    const int32_t platformMidMaxFP = 600 * FP_SCALE;
    const int32_t platformTopYFP  = 420 * FP_SCALE;
    const int32_t platformTopLMinFP = 100 * FP_SCALE;
    const int32_t platformTopLMaxFP = 300 * FP_SCALE;
    const int32_t platformTopRMinFP = 500 * FP_SCALE;
    const int32_t platformTopRMaxFP = 700 * FP_SCALE;

    for (auto& p : g_players) {
        if (!p.connected) continue;

        if (!p.alive) {
            if (p.lives > 0 && g_tick >= p.respawnTick) {
                p.alive = true;
                p.health = PLAYER_MAX_HEALTH;
                SetSpawnPosition(p);
            }
            continue;
        }

        int32_t oldY = p.y;

        int32_t moveX = 0;
        if (p.keys & INPUT_ROTATE_LEFT)  moveX -= FIGHTER_MOVE_SPEED_FP;
        if (p.keys & INPUT_ROTATE_RIGHT) moveX += FIGHTER_MOVE_SPEED_FP;
        p.velX = moveX;

        bool onSurface = false;
        if (oldY == groundYFP && p.x >= groundXMinFP && p.x <= groundXMaxFP)
            onSurface = true;
        if (oldY == platformMidYFP && p.x >= platformMidMinFP && p.x <= platformMidMaxFP)
            onSurface = true;
        if (oldY == platformTopYFP &&
            ((p.x >= platformTopLMinFP && p.x <= platformTopLMaxFP) ||
             (p.x >= platformTopRMinFP && p.x <= platformTopRMaxFP)))
            onSurface = true;

        if (onSurface) {
            if (p.velY < 0) p.velY = 0;
            if (p.keys & INPUT_THRUST) {
                p.velY = +FIGHTER_JUMP_SPEED_FP;
            }
        }

        p.velY -= FIGHTER_GRAVITY_FP;

        static std::array<uint32_t, MAX_PLAYERS> lastFire{};
        if ((p.keys & INPUT_FIRE) && g_tick - lastFire[p.id - 1] > 15) {
            lastFire[p.id - 1] = g_tick;
            uint8_t bid = NextBulletId();
            ServerBullet* bs = GetFreeBulletSlot();

            if (bid && bs) {
                float rad = DegToRad(static_cast<float>(p.angle) / 10.0f);
                bs->id = bid;
                bs->ownerId = p.id;
                bs->x = p.x;
                bs->y = p.y;
                bs->velX = static_cast<int32_t>(std::cos(rad) * BULLET_SPEED_FP);
                bs->velY = static_cast<int32_t>(std::sin(rad) * BULLET_SPEED_FP);
                bs->birthTick = g_tick;
                bs->alive = true;

                PktBulletSpawn bpkt{};
                bpkt.hdr.type = MSG_BULLET_SPAWN;
                bpkt.hdr.seq = htons(SEQ_UNRELIABLE);
                bpkt.hdr.timestamp = htonl(g_tick);
                bpkt.bullet.id = bid;
                bpkt.bullet.ownerId = p.id;
                bpkt.bullet.alive = 1;
                bpkt.bullet.x = htonl(bs->x);
                bpkt.bullet.y = htonl(bs->y);
                bpkt.bullet.velX = htons(static_cast<uint16_t>(bs->velX));
                bpkt.bullet.velY = htons(static_cast<uint16_t>(bs->velY));
                Broadcast(&bpkt, sizeof(bpkt));
            }
        }

        p.x += p.velX;
        p.y += p.velY;

        auto LandOn = [&](int32_t surfY, int32_t minX, int32_t maxX) {
            if (oldY >= surfY && p.y <= surfY && p.velY <= 0 &&
                p.x >= minX && p.x <= maxX) {
                p.y = surfY;
                if (p.velY < 0) p.velY = 0;
            }
        };

        LandOn(platformTopYFP, platformTopLMinFP, platformTopLMaxFP);
        LandOn(platformTopYFP, platformTopRMinFP, platformTopRMaxFP);
        LandOn(platformMidYFP, platformMidMinFP, platformMidMaxFP);
        LandOn(groundYFP, groundXMinFP, groundXMaxFP);

        if (p.x < 0) p.x = 0;
        if (p.x > worldWidthFP) p.x = worldWidthFP;
        if (p.y < groundYFP) {
            p.y = groundYFP;
            if (p.velY < 0) p.velY = 0;
        }
    }

    const int32_t worldHeightFP = WORLD_H * FP_SCALE;

    for (auto& b : g_bullets) {
        if (!b.alive) continue;
        if (g_tick - b.birthTick > BULLET_LIFETIME) {
            b.alive = false;
            continue;
        }

        b.x += b.velX;
        b.y += b.velY;

        if (b.x < 0 || b.x > worldWidthFP || b.y < 0 || b.y > worldHeightFP) {
            b.alive = false;
            continue;
        }
    }

    CheckBulletPlayerCollisions();

    bool anyLivesLeft = false;
    for (auto& p : g_players) {
        if (p.connected && p.lives > 0) {
            anyLivesLeft = true;
            break;
        }
    }
    if (!anyLivesLeft && CountConnected() > 0 && g_phase == GamePhase::PLAYING) {
        EndRound();
    }
}

// ── Timeout detection ─────────────────────────────────────────────────────────
static void CheckTimeouts() {
    constexpr uint32_t TIMEOUT_TICKS = 300;
    for (auto& p : g_players) {
        if (!p.connected) continue;
        if (g_tick - p.lastSeenTick > TIMEOUT_TICKS) {
            std::cout << "[Server] Player " << static_cast<int>(p.id) << " timed out.\n";
            HandleQuit(p.addr);
        }
    }
}

// ── Game loop ─────────────────────────────────────────────────────────────────
static void GameLoop() {
    using Clock = std::chrono::steady_clock;
    auto tickDuration = std::chrono::microseconds(1000000 / g_tickRateHz);
    auto nextTick = Clock::now();

    while (g_running) {
        auto now = Clock::now();
        if (now < nextTick) {
            std::this_thread::sleep_until(nextTick);
        }
        nextTick += tickDuration;

        std::lock_guard<std::mutex> lock(g_mtx);
        ++g_tick;

        if (g_phase == GamePhase::PLAYING) {
            UpdatePhysics();
            BroadcastGameState();

            if (g_tick % 30 == 0) {
                BroadcastScores();
            }

            RetransmitPending();

            g_pendingAcks.erase(
                std::remove_if(g_pendingAcks.begin(), g_pendingAcks.end(),
                    [](const ReliableMsg& rm) { return AllAcked(rm); }),
                g_pendingAcks.end());
        }
        else if (g_phase == GamePhase::ROUND_OVER) {
            RetransmitPending();

            g_pendingAcks.erase(
                std::remove_if(g_pendingAcks.begin(), g_pendingAcks.end(),
                    [](const ReliableMsg& rm) { return AllAcked(rm); }),
                g_pendingAcks.end());
        }

        if (g_tick % 90 == 0) {
            CheckTimeouts();
        }
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    LoadConfig("server_config.txt");

    if (!InitNetworking()) {
        std::cerr << "[Server] Failed to initialise networking.\n";
        return 1;
    }

    LoadLeaderboard();

    std::cout << "=== Spaceships Server ===\n";
    std::cout << "Server UDP Port [" << g_serverPort << "]: ";

    std::string line;
    std::getline(std::cin, line);
    if (!line.empty()) {
        g_serverPort = static_cast<uint16_t>(std::stoi(line));
    }

    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
        std::cerr << "[Server] socket() failed.\n";
        CleanupNetworking();
        return 1;
    }

    int opt = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = htons(g_serverPort);

    if (bind(g_sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) == SOCKET_ERROR) {
        std::cerr << "[Server] bind() failed on port " << g_serverPort << ".\n";
        closesocket(g_sock);
        CleanupNetworking();
        return 1;
    }

#ifdef _WIN32
    DWORD tv = 100;
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
#endif

    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&tv), sizeof(tv));

    std::cout << "[Server] Listening on UDP port " << g_serverPort
        << " | Tick rate: " << g_tickRateHz << " Hz"
        << " | Waiting for " << g_minPlayers << "+" << " players...\n";

    std::thread recvThread(RecvThread);
    GameLoop();

    g_running = false;
    if (recvThread.joinable()) recvThread.join();

    closesocket(g_sock);
    CleanupNetworking();
    return 0;
}