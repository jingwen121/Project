#include "../common/game_protocol.h"
#include "../common/game_math.h"
#include "AEEngine.h"
#include <cstdio>

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstring>
#include <chrono>
#include <cmath>

struct Fighter
{
    float x;
    float y;
    float vx;
    float vy;
    int   facing;
    int   health;
    unsigned int color;
};

// use MAX_PLAYERS to match server capacity
static std::array<Fighter, MAX_PLAYERS> g_fighters{};
static int g_playerCount = 0;
static AEGfxVertexList* g_rectMesh = nullptr;

static SOCKET     g_sock  = INVALID_SOCKET;
static sockaddr_in g_serverAddr{};
static std::atomic<bool> g_netRunning{ true };
static std::atomic<bool> g_joined{ false };
static uint8_t  g_myId = 0;
static uint32_t g_localTick = 0;
static uint32_t g_lastServerTick = 0;

struct NetPlayer {
    uint8_t playerId; // store server id (1-based)
    float x;
    float y;
    uint16_t score;
    uint8_t alive;
    uint8_t lives;
    uint8_t health;
    char name[16];
    bool active;
};

struct NetBullet {
    float x;
    float y;
    bool  active;
};

static std::array<NetPlayer, MAX_PLAYERS> g_netPlayers{};
static std::array<NetBullet, MAX_BULLETS> g_netBullets{};
static uint8_t g_netPlayerCount = 0;
static std::mutex g_netStateMtx;
static std::atomic<int16_t> g_lastAimAngle{0};
static s8 g_fontId = -1;
static std::atomic<bool> g_roundOverActive{ false };
static std::mutex g_roundOverMtx;
static uint8_t g_roundWinnerCount = 0;
static std::array<PktScoreUpdate::Entry, MAX_PLAYERS> g_roundWinners{};
static uint8_t g_roundLeaderboardCount = 0;
static std::array<LeaderboardEntry, LEADERBOARD_SIZE> g_roundLeaderboard{};

static const char* PlayerColorLabel(uint8_t playerId)
{
    switch (playerId)
    {
        case 1: return "Red";
        case 2: return "Green";
        case 3: return "Blue";
        case 4: return "White";
        case 5: return "Cyan";
        case 6: return "Magenta";
        default: return "";
    }
}

static char ToLowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c + ('a' - 'A'));
    return c;
}

static bool ContainsIgnoreCase(const char* haystack, const char* needle)
{
    if (!haystack || !needle || needle[0] == '\0')
        return false;

    for (const char* h = haystack; *h; ++h)
    {
        const char* a = h;
        const char* b = needle;
        while (*a && *b && ToLowerAscii(*a) == ToLowerAscii(*b))
        {
            ++a;
            ++b;
        }
        if (*b == '\0')
            return true;
    }

    return false;
}

static void SendInput(uint8_t keys);
static void SendJoin();
static void SendPing();
static void StartNetworking();
static void StopNetworking();

static void InitPlayers()
{
    float minX = AEGfxGetWinMinX();
    float maxX = AEGfxGetWinMaxX();
    float minY = AEGfxGetWinMinY();
    float maxY = AEGfxGetWinMaxY();

    float midX = 0.5f * (minX + maxX);
    float midY = 0.5f * (minY + maxY);

    for (int i = 0; i < static_cast<int>(g_fighters.size()); ++i)
    {
        float offset = (maxX - minX) * 0.2f;
        g_fighters[i].x = (i % 2 == 0) ? (midX - offset) : (midX + offset);
        g_fighters[i].y = midY;
        g_fighters[i].vx = 0.0f;
        g_fighters[i].vy = 0.0f;
        g_fighters[i].facing = (i % 2 == 0) ? 1 : -1;
        g_fighters[i].health = 100;

        switch (i)
        {
        case 0: g_fighters[i].color = 0xFFFF0000; break;
        case 1: g_fighters[i].color = 0xFF00FF00; break;
        case 2: g_fighters[i].color = 0xFF0000FF; break;
        default: g_fighters[i].color = 0xFFFFFFFF; break;
        }
    }

    // Debug: print initial window bounds and first two fighters
    printf("InitPlayers: winMinX=%f winMaxX=%f winMinY=%f winMaxY=%f\n",
           AEGfxGetWinMinX(), AEGfxGetWinMaxX(), AEGfxGetWinMinY(), AEGfxGetWinMaxY());
    for (int i = 0; i < 2 && i < static_cast<int>(g_fighters.size()); ++i)
        printf("Player %d initial pos: x=%f y=%f\n", i, g_fighters[i].x, g_fighters[i].y);

}

static void CreateRectangleMesh()
{
    u32 color = 0xFFFFFFFF;
    AEGfxMeshStart();
    AEGfxTriAdd(-0.5f, -0.5f, color, 0.0f, 0.0f,
                0.5f, -0.5f, color, 1.0f, 0.0f,
                0.5f,  0.5f, color, 1.0f, 1.0f);
    AEGfxTriAdd(-0.5f, -0.5f, color, 0.0f, 0.0f,
                0.5f,  0.5f, color, 1.0f, 1.0f,
                -0.5f, 0.5f, color, 0.0f, 1.0f);
    g_rectMesh = AEGfxMeshEnd();
}

static void HandleInputLocal()
{
    if (g_roundOverActive.load())
    {
        ++g_localTick;
        return;
    }

    uint8_t keys = 0;

    // INPUT_ROTATE_LEFT  → move left
    // INPUT_ROTATE_RIGHT → move right
    // INPUT_THRUST       → jump
    // INPUT_FIRE         → shoot

    if (AEInputCheckCurr(AEVK_A) || AEInputCheckCurr(AEVK_LEFT))
        keys |= INPUT_ROTATE_LEFT;
    if (AEInputCheckCurr(AEVK_D) || AEInputCheckCurr(AEVK_RIGHT))
        keys |= INPUT_ROTATE_RIGHT;
    if (AEInputCheckCurr(AEVK_W) || AEInputCheckCurr(AEVK_UP))
        keys |= INPUT_THRUST;
    if (AEInputCheckCurr(AEVK_SPACE) || AEInputCheckCurr(AEVK_LBUTTON))
        keys |= INPUT_FIRE;

    int16_t aimAngle = 0;
    if (g_joined)
    {
        s32 mx = 0, my = 0;
        AEInputGetCursorPosition(&mx, &my);

        float px = 0.0f, py = 0.0f;
        {
            std::lock_guard<std::mutex> lock(g_netStateMtx);
            int idx = (g_myId > 0 && g_myId <= MAX_PLAYERS) ? (g_myId - 1) : 0;
            px = g_netPlayers[idx].x;
            py = g_netPlayers[idx].y;
        }

        float winW = 1200.0f;
        float winH = 720.0f;
        float nx = static_cast<float>(mx) / winW;
        float ny = static_cast<float>(my) / winH;
        float mxWorld = nx * static_cast<float>(WORLD_W);
        float myWorld = (1.0f - ny) * static_cast<float>(WORLD_H);

        float dx = mxWorld - px;
        float dy = myWorld - py;
        if (dx != 0.0f || dy != 0.0f)
        {
            float angRad = std::atan2(dy, dx);
            float angDeg = angRad * 180.0f / 3.14159265f;
            if (angDeg < 0.0f)
                angDeg += 360.0f;
            aimAngle = static_cast<int16_t>(angDeg * 10.0f);
        }
    }

    g_lastAimAngle.store(aimAngle);

    if (g_joined && g_sock != INVALID_SOCKET)
        SendInput(keys);

    ++g_localTick;
}

static void ClampToArena()
{
    float minX = AEGfxGetWinMinX();
    float maxX = AEGfxGetWinMaxX();
    float minY = AEGfxGetWinMinY();
    float maxY = AEGfxGetWinMaxY();

    float left = minX + (maxX - minX) * 0.1f;
    float right = maxX - (maxX - minX) * 0.1f;
    float groundY = minY + (maxY - minY) * 0.1f;

    // Use a fighter size relative to the window so fighters are visible
    float fighterHeight = (maxY - minY) * 0.15f; // 15% of window height
    float fighterHalfH = fighterHeight * 0.5f;

    // Ground visual height matches DrawArena's approach
    float groundHeight = (maxY - minY) * 0.05f;
    float groundHalfH = groundHeight * 0.5f;

    for (int i = 0; i < static_cast<int>(g_fighters.size()); ++i)
    {
        Fighter& f = g_fighters[i];

        if (f.x < left)  f.x = left;
        if (f.x > right) f.x = right;

        // Place fighter so their feet sit on top of the ground rectangle.
        f.y = groundY + groundHalfH + fighterHalfH;
    }
}

static void SyncFightersFromNet()
{
    std::lock_guard<std::mutex> lock(g_netStateMtx);

    // Count active and map server state into local fighter slots using playerId
    int activeCount = 0;
    float minX = AEGfxGetWinMinX();
    float maxX = AEGfxGetWinMaxX();
    float minY = AEGfxGetWinMinY();
    float maxY = AEGfxGetWinMaxY();
    float width = maxX - minX;
    float height = maxY - minY;

    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        const NetPlayer& np = g_netPlayers[id];
        Fighter& f = g_fighters[id];

        if (!np.active)
        {
            f.health = 0;
            continue;
        }

        float nx = np.x / static_cast<float>(WORLD_W);
        float ny = np.y / static_cast<float>(WORLD_H);

        f.x = minX + nx * width;
        f.y = minY + ny * height;
        f.health = static_cast<int>(np.health);
        ++activeCount;
    }

    g_playerCount = activeCount;
}

static void UpdateGame()
{
    HandleInputLocal();
    SyncFightersFromNet();
}

static void DrawFilledRect(float cx, float cy, float width, float height, unsigned int color)
{
    AEMtx33 scale;
    AEMtx33Scale(&scale, width, height);
    AEMtx33 translate;
    AEMtx33Trans(&translate, cx, cy);
    AEMtx33 m;
    AEMtx33Concat(&m, &translate, &scale);

    AEGfxSetTransform(m.m);
    AEGfxSetRenderMode(AE_GFX_RM_COLOR);
    AEGfxSetBlendMode(AE_GFX_BM_NONE);
    AEGfxSetTransparency(1.0f);

    float a = ((color >> 24) & 0xFF) / 255.0f;
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    AEGfxSetColorToMultiply(r, g, b, a);

    if (g_rectMesh)
        AEGfxMeshDraw(g_rectMesh, AE_GFX_MDM_TRIANGLES);
}

static void DrawArena()
{
    float minX = AEGfxGetWinMinX();
    float maxX = AEGfxGetWinMaxX();
    float minY = AEGfxGetWinMinY();
    float maxY = AEGfxGetWinMaxY();

    AEGfxSetRenderMode(AE_GFX_RM_COLOR);
    AEGfxSetBlendMode(AE_GFX_BM_NONE);
    AEGfxSetTransparency(1.0f);

    float left = minX + (maxX - minX) * 0.05f;
    float right = maxX - (maxX - minX) * 0.05f;
    float groundY = minY + (maxY - minY) * 0.1f;

    float cx = 0.5f * (left + right);
    float width = right - left;
    float height = (maxY - minY) * 0.05f;

    // Make the ground brighter so it's visible against the background
    DrawFilledRect(cx, groundY, width, height, 0xFF808080);
}

static void DrawFighters()
{
    float minX = AEGfxGetWinMinX();
    float maxX = AEGfxGetWinMaxX();
    float minY = AEGfxGetWinMinY();
    float maxY = AEGfxGetWinMaxY();

    AEGfxSetRenderMode(AE_GFX_RM_COLOR);
    AEGfxSetBlendMode(AE_GFX_BM_NONE);
    AEGfxSetTransparency(1.0f);

    float width = (maxX - minX) * 0.05f;
    float height = (maxY - minY) * 0.15f;

    bool anyActive = false;
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (!g_netPlayers[i].active)
            continue;
        anyActive = true;
        Fighter& f = g_fighters[i];
        DrawFilledRect(f.x, f.y, width, height, f.color);

        if (f.health > 0)
        {
            float hpFrac = static_cast<float>(f.health) / static_cast<float>(PLAYER_MAX_HEALTH);
            if (hpFrac < 0.0f) hpFrac = 0.0f;
            if (hpFrac > 1.0f) hpFrac = 1.0f;

            float barHeight = (maxY - minY) * 0.02f;
            float barY = f.y + height * 0.6f;

            DrawFilledRect(f.x, barY, width, barHeight, 0xFF404040);

            float barFullLeft  = f.x - width * 0.5f;
            float barCurrWidth = width * hpFrac;
            float barCurrCenter = barFullLeft + barCurrWidth * 0.5f;
            DrawFilledRect(barCurrCenter, barY, barCurrWidth, barHeight, 0xFF00FF00);
        }
    }

    if (!anyActive)
    {
        int localCount = static_cast<int>(g_fighters.size());
        for (int i = 0; i < localCount; ++i)
        {
            Fighter& f = g_fighters[i];
            DrawFilledRect(f.x, f.y, width, height, f.color);
        }
    }
}

static void DrawBullets()
{
    float minX = AEGfxGetWinMinX();
    float maxX = AEGfxGetWinMaxX();
    float minY = AEGfxGetWinMinY();
    float maxY = AEGfxGetWinMaxY();

    AEGfxSetRenderMode(AE_GFX_RM_COLOR);
    AEGfxSetBlendMode(AE_GFX_BM_NONE);
    AEGfxSetTransparency(1.0f);

    float width = (maxX - minX) * 0.015f;
    float height = (maxY - minY) * 0.04f;

    for (int i = 0; i < MAX_BULLETS; ++i)
    {
        if (!g_netBullets[i].active)
            continue;

        float nx = g_netBullets[i].x / static_cast<float>(WORLD_W);
        float ny = g_netBullets[i].y / static_cast<float>(WORLD_H);
        float x = minX + nx * (maxX - minX);
        float y = minY + ny * (maxY - minY);
        DrawFilledRect(x, y, width, height, 0xFFFFFF00);
    }
}

static void DrawFrame()
{
    static bool s_debugPrinted = false;

    AEGfxSetBackgroundColor(0.05f, 0.05f, 0.1f);
    DrawArena();
    DrawFighters();
    DrawBullets();

    // HUD during gameplay (live scores + local lives).
    if (g_fontId >= 0 && !g_roundOverActive.load())
    {
        AEGfxSetBlendMode(AE_GFX_BM_BLEND);

        AEGfxPrint(g_fontId, "Scores", -0.95f, 0.92f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f);

        float y = 0.84f;
        std::lock_guard<std::mutex> lock(g_netStateMtx);
        for (int i = 0; i < MAX_PLAYERS; ++i)
        {
            if (!g_netPlayers[i].active)
                continue;

            char line[96]{};
            std::snprintf(line, sizeof(line), "%s - %u", g_netPlayers[i].name, static_cast<unsigned int>(g_netPlayers[i].score));
            AEGfxPrint(g_fontId, line, -0.95f, y, 0.55f, 0.9f, 0.9f, 0.9f, 1.0f);
            y -= 0.07f;
        }

        // Local lives indicator (3 boxes). Boxes disappear from the left as lives decrease.
        uint8_t lives = 0;
        unsigned int lifeColor = 0xFFFFFFFF;
        if (g_myId > 0 && g_myId <= MAX_PLAYERS)
        {
            int idx = static_cast<int>(g_myId - 1);
            lives = g_netPlayers[idx].lives;
            lifeColor = g_fighters[idx].color;
        }

        float minX = AEGfxGetWinMinX();
        float maxX = AEGfxGetWinMaxX();
        float minY = AEGfxGetWinMinY();
        float maxY = AEGfxGetWinMaxY();
        float w = maxX - minX;
        float h = maxY - minY;

        float boxSize = w * 0.03f;
        float gap = w * 0.01f;
        float marginX = w * 0.02f;
        float marginY = h * 0.06f;

        float topY = maxY - marginY;
        float startX = (maxX - marginX) - (3.0f * boxSize + 2.0f * gap);

        for (int i = 0; i < 3; ++i)
        {
            bool show = (lives > 0) && (i >= (3 - static_cast<int>(lives)));
            if (!show)
                continue;

            float x = startX + i * (boxSize + gap) + boxSize * 0.5f;
            DrawFilledRect(x, topY, boxSize, boxSize, lifeColor);
        }
    }

    if (g_fontId >= 0 && g_roundOverActive.load())
    {
        AEGfxSetBlendMode(AE_GFX_BM_BLEND);

        AEGfxPrint(g_fontId, "ROUND OVER", -0.25f, 0.85f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

        std::lock_guard<std::mutex> lock(g_roundOverMtx);

        float y = 0.70f;
        AEGfxPrint(g_fontId, "Winners", -0.95f, y, 0.7f, 1.0f, 0.9f, 0.2f, 1.0f);
        y -= 0.10f;

        for (int i = 0; i < static_cast<int>(g_roundWinnerCount) && i < MAX_PLAYERS; ++i)
        {
            char line[128]{};
            uint16_t score = ntohs(g_roundWinners[i].score);
            const char* color = PlayerColorLabel(g_roundWinners[i].playerId);
            const bool nameAlreadyHasColor = (color[0] != '\0') && ContainsIgnoreCase(g_roundWinners[i].name, color);

            if (color[0] != '\0' && !nameAlreadyHasColor)
                std::snprintf(line, sizeof(line), "%d) %s %s - %u", i + 1, g_roundWinners[i].name, color, static_cast<unsigned int>(score));
            else
                std::snprintf(line, sizeof(line), "%d) %s - %u", i + 1, g_roundWinners[i].name, static_cast<unsigned int>(score));
            AEGfxPrint(g_fontId, line, -0.95f, y, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f);
            y -= 0.08f;
        }

        y = 0.70f;
        AEGfxPrint(g_fontId, "Top 5 All-Time", 0.10f, y, 0.7f, 0.2f, 0.9f, 1.0f, 1.0f);
        y -= 0.10f;

        for (int i = 0; i < static_cast<int>(g_roundLeaderboardCount) && i < LEADERBOARD_SIZE; ++i)
        {
            char line[128]{};
            std::snprintf(line, sizeof(line), "%d) %s - %u", i + 1, g_roundLeaderboard[i].name, static_cast<unsigned int>(g_roundLeaderboard[i].score));
            AEGfxPrint(g_fontId, line, 0.10f, y, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f);
            y -= 0.06f;

            char when[64]{};
            std::snprintf(when, sizeof(when), "%s", g_roundLeaderboard[i].datetime);
            AEGfxPrint(g_fontId, when, 0.10f, y, 0.45f, 0.7f, 0.7f, 0.7f, 1.0f);
            y -= 0.07f;
        }

        AEGfxPrint(g_fontId, "Close the window to exit", -0.30f, -0.95f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Print debug info once after first frame so we can confirm positions
    if (!s_debugPrinted)
    {
        float minX = AEGfxGetWinMinX();
        float maxX = AEGfxGetWinMaxX();
        float minY = AEGfxGetWinMinY();
        float maxY = AEGfxGetWinMaxY();
        printf("DrawFrame: winMinX=%f winMaxX=%f winMinY=%f winMaxY=%f\n", minX, maxX, minY, maxY);
        for (int i = 0; i < static_cast<int>(g_fighters.size()); ++i)
        {
            printf("Player slot %d pos after Clamp: x=%f y=%f active=%d\n", i, g_fighters[i].x, g_fighters[i].y,
                   g_netPlayers[i].active ? 1 : 0);
        }
        s_debugPrinted = true;
    }
}

static void SendTo(const void* data, int len)
{
    sendto(g_sock, reinterpret_cast<const char*>(data), len, 0,
           reinterpret_cast<const sockaddr*>(&g_serverAddr), sizeof(g_serverAddr));
}

static void SendJoin()
{
    PktJoin pkt{};
    pkt.hdr.type     = MSG_JOIN;
    pkt.hdr.playerId = 0;
    pkt.hdr.seq      = htons(SEQ_UNRELIABLE);
    pkt.hdr.timestamp = htonl(g_localTick);
    strncpy_s(pkt.name, sizeof(pkt.name), "Player", _TRUNCATE);
    SendTo(&pkt, sizeof(pkt));
}

static void SendInput(uint8_t keys)
{
    PktInput pkt{};
    pkt.hdr.type     = MSG_INPUT;
    pkt.hdr.playerId = g_myId;
    pkt.hdr.seq      = htons(SEQ_UNRELIABLE);
    pkt.hdr.timestamp = htonl(g_localTick);
    pkt.keys         = keys;
    pkt.aimAngle     = htons(g_lastAimAngle.load());
    SendTo(&pkt, sizeof(pkt));
}

static void SendPing()
{
    using Clock = std::chrono::steady_clock;
    static std::atomic<uint32_t> s_pingSeq{0};
    PktPing pkt{};
    pkt.hdr.type = MSG_PING;
    pkt.hdr.seq  = htons(static_cast<uint16_t>(++s_pingSeq));
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
    pkt.clientTime = now;
    SendTo(&pkt, sizeof(pkt));
}

static void RecvThread()
{
    char buf[2048];
    sockaddr_in from{};

    while (g_netRunning)
    {
        int fromLen = sizeof(from);
        int r = recvfrom(g_sock, buf, sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (r < static_cast<int>(sizeof(PktHeader)))
            continue;

        PktHeader hdr;
        std::memcpy(&hdr, buf, sizeof(PktHeader));

        switch (static_cast<MsgType>(hdr.type))
        {
        case MSG_JOIN_ACK:
        case MSG_REJOIN_ACK:
        {
            PktJoinAck ack;
            std::memcpy(&ack, buf, sizeof(PktJoinAck));
            g_myId = ack.assignedId;
            g_joined = true;
            break;
        }
        case MSG_GAME_STATE:
        {
            PktGameState gs;
            std::memcpy(&gs, buf, sizeof(PktGameState));
            std::lock_guard<std::mutex> lock(g_netStateMtx);
            g_lastServerTick  = ntohl(gs.gameTick);

            for (int i = 0; i < MAX_PLAYERS; ++i) g_netPlayers[i].active = false;
            for (int i = 0; i < MAX_BULLETS; ++i) g_netBullets[i].active = false;

            for (int i = 0; i < gs.playerCount && i < MAX_PLAYERS; ++i)
            {
                const PlayerState& ps = gs.players[i];
                int idIndex = (ps.playerId > 0 && ps.playerId <= MAX_PLAYERS) ? (ps.playerId - 1) : i;
                auto& np = g_netPlayers[idIndex];
                np.playerId = ps.playerId;
                np.x = static_cast<float>(static_cast<int32_t>(ntohl(ps.x))) / FP_SCALE;
                np.y = static_cast<float>(static_cast<int32_t>(ntohl(ps.y))) / FP_SCALE;
                np.score = ntohs(ps.score);
                np.alive = ps.alive;
                np.lives = ps.lives;
                np.health = ps.health;
                strncpy_s(np.name, sizeof(np.name), ps.name, _TRUNCATE);
                np.active = (ps.lives > 0);
            }

            for (int i = 0; i < gs.bulletCount && i < MAX_BULLETS; ++i)
            {
                const BulletState& bs = gs.bullets[i];
                auto& nb = g_netBullets[i];
                nb.x = static_cast<float>(static_cast<int32_t>(ntohl(bs.x))) / FP_SCALE;
                nb.y = static_cast<float>(static_cast<int32_t>(ntohl(bs.y))) / FP_SCALE;
                nb.active = bs.alive != 0;
            }

            int cnt = 0;
            for (int i = 0; i < MAX_PLAYERS; ++i) if (g_netPlayers[i].active) ++cnt;
            g_netPlayerCount = static_cast<uint8_t>(cnt);

            break;
        }
        case MSG_ROUND_OVER:
        {
            if (r < static_cast<int>(sizeof(PktRoundOver)))
                break;

            PktRoundOver ro{};
            std::memcpy(&ro, buf, sizeof(PktRoundOver));

            {
                std::lock_guard<std::mutex> lock(g_roundOverMtx);

                g_roundWinnerCount = ro.winnerCount;
                for (int i = 0; i < MAX_PLAYERS; ++i)
                    g_roundWinners[i] = ro.winners[i];

                g_roundLeaderboardCount = ro.leaderboardCount;
                for (int i = 0; i < LEADERBOARD_SIZE; ++i)
                    g_roundLeaderboard[i] = ro.leaderboard[i];
            }

            g_roundOverActive.store(true);

            uint16_t seq = ntohs(ro.hdr.seq);
            if (seq != SEQ_UNRELIABLE)
            {
                PktAck ack{};
                ack.hdr.type = MSG_ACK;
                ack.hdr.playerId = g_myId;
                ack.hdr.seq = htons(SEQ_UNRELIABLE);
                ack.hdr.timestamp = htonl(g_localTick);
                ack.ackSeq = htons(seq);
                SendTo(&ack, sizeof(ack));
            }

            break;
        }
        default:
            break;
        }
    }
}

static void PingThread()
{
    while (g_netRunning)
    {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (g_joined && g_sock != INVALID_SOCKET)
            SendPing();
    }
}

static void StartNetworking()
{
    if (!InitNetworking())
        return;

    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET)
        return;

    sockaddr_in bindAddr{};
    bindAddr.sin_family      = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port        = htons(GAME_PORT_CLIENT);
    bind(g_sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));

#ifdef _WIN32
    DWORD tv = 50;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    g_serverAddr.sin_family = AF_INET;
    g_serverAddr.sin_port   = htons(GAME_PORT);
    inet_pton(AF_INET, "127.0.0.1", &g_serverAddr.sin_addr);

    SendJoin();

    std::thread recvThr(RecvThread);
    std::thread pingThr(PingThread);
    recvThr.detach();
    pingThr.detach();
}

static void StopNetworking()
{
    g_netRunning = false;
    if (g_sock != INVALID_SOCKET)
    {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    CleanupNetworking();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    int winWidth = 1200;
    int winHeight = 720;
    int createConsole = 1;
    unsigned int maxFps = 60;
    bool vsync = true;

    if (!AESysInit(hInstance, nCmdShow, winWidth, winHeight,
        createConsole, maxFps, vsync, 0))
    {
        return -1;
    }

    AESysSetWindowTitle("2-4 Player Fighting Game");

    g_fontId = AEGfxCreateFont("C:\\Windows\\Fonts\\arial.ttf", 24);

    CreateRectangleMesh();
    InitPlayers();
    StartNetworking();

    while (AESysDoesWindowExist())
    {
        AESysFrameStart();

        UpdateGame();
        DrawFrame();

        AESysFrameEnd();
    }

    StopNetworking();

    if (g_rectMesh)
        AEGfxMeshFree(g_rectMesh);

    if (g_fontId >= 0)
        AEGfxDestroyFont(g_fontId);

    AESysExit();
    return 0;
}