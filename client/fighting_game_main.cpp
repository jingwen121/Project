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

static Fighter g_fighters[4];
static int g_playerCount = 2;
static AEGfxVertexList* g_rectMesh = nullptr;

static SOCKET     g_sock  = INVALID_SOCKET;
static sockaddr_in g_serverAddr{};
static std::atomic<bool> g_netRunning{ true };
static std::atomic<bool> g_joined{ false };
static uint8_t  g_myId = 0;
static uint32_t g_localTick = 0;
static uint32_t g_lastServerTick = 0;

struct NetPlayer {
    float x;
    float y;
    uint16_t score;
    uint8_t alive;
    char name[16];
    bool active;
};

static std::array<NetPlayer, MAX_PLAYERS> g_netPlayers{};
static uint8_t g_netPlayerCount = 0;
static std::mutex g_netStateMtx;

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

    for (int i = 0; i < 4; ++i)
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
    printf("InitPlayers: winMinX=%f winMaxX=%f winMinY=%f winMaxY=%f\n", minX, maxX, minY, maxY);
    for (int i = 0; i < 2; ++i)
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
    uint8_t keys = 0;

    if (AEInputCheckCurr(AEVK_A) || AEInputCheckCurr(AEVK_LEFT))
        keys |= INPUT_ROTATE_LEFT;
    if (AEInputCheckCurr(AEVK_D) || AEInputCheckCurr(AEVK_RIGHT))
        keys |= INPUT_ROTATE_RIGHT;
    if (AEInputCheckCurr(AEVK_W) || AEInputCheckCurr(AEVK_UP))
        keys |= INPUT_THRUST;
    if (AEInputCheckCurr(AEVK_SPACE))
        keys |= INPUT_FIRE;

    if (g_joined && g_sock != INVALID_SOCKET && keys != 0)
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

    for (int i = 0; i < g_playerCount; ++i)
    {
        Fighter& f = g_fighters[i];

        if (f.x < left)  f.x = left;
        if (f.x > right) f.x = right;

        // Place fighter so their feet sit on top of the ground rectangle.
        // NOTE: add (not subtract) because y increases upward in world coords.
        f.y = groundY + groundHalfH + fighterHalfH;
    }
}

static void SyncFightersFromNet()
{
    std::lock_guard<std::mutex> lock(g_netStateMtx);

    float minX = AEGfxGetWinMinX();
    float maxX = AEGfxGetWinMaxX();
    float minY = AEGfxGetWinMinY();
    float maxY = AEGfxGetWinMaxY();

    float width = maxX - minX;
    float height = maxY - minY;

    g_playerCount = g_netPlayerCount;

    for (int i = 0; i < g_playerCount; ++i)
    {
        const NetPlayer& np = g_netPlayers[i];
        Fighter& f = g_fighters[i];

        float nx = np.x / static_cast<float>(WORLD_W);
        float ny = np.y / static_cast<float>(WORLD_H);

        f.x = minX + nx * width;
        f.y = minY + ny * height;
        f.health = np.alive ? 100 : 0;
    }
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

    // Set render state once for all fighters
    AEGfxSetRenderMode(AE_GFX_RM_COLOR);
    AEGfxSetBlendMode(AE_GFX_BM_NONE);
    AEGfxSetTransparency(1.0f);

    // Use sizes relative to window so fighters are visible at any resolution
    float width = (maxX - minX) * 0.05f;   // 5% of window width
    float height = (maxY - minY) * 0.15f;  // 15% of window height

    for (int i = 0; i < g_playerCount; ++i)
    {
        Fighter& f = g_fighters[i];
        DrawFilledRect(f.x, f.y, width, height, f.color);
    }
}

static void DrawFrame()
{
    static bool s_debugPrinted = false;

    AEGfxSetBackgroundColor(0.05f, 0.05f, 0.1f);
    DrawArena();
    DrawFighters();

    // Print debug info once after first frame so we can confirm positions
    if (!s_debugPrinted)
    {
        float minX = AEGfxGetWinMinX();
        float maxX = AEGfxGetWinMaxX();
        float minY = AEGfxGetWinMinY();
        float maxY = AEGfxGetWinMaxY();
        printf("DrawFrame: winMinX=%f winMaxX=%f winMinY=%f winMaxY=%f\n", minX, maxX, minY, maxY);
        for (int i = 0; i < g_playerCount; ++i)
        {
            printf("Player %d pos after Clamp: x=%f y=%f\n", i, g_fighters[i].x, g_fighters[i].y);
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
    SendTo(&pkt, sizeof(pkt));
}

static void SendPing()
{
    using Clock = std::chrono::steady_clock;
    static std::atomic<uint32_t> s_pingSeq{0};
    PktPing pkt{};
    pkt.hdr.type = MSG_PING;
    pkt.hdr.seq  = htons(static_cast<uint16_t>(++s_pingSeq));
    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                       Clock::now().time_since_epoch()).count();
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
            g_netPlayerCount  = gs.playerCount;
            for (int i = 0; i < gs.playerCount && i < MAX_PLAYERS; ++i)
            {
                auto& ps = gs.players[i];
                auto& np = g_netPlayers[i];
                np.x = static_cast<float>(static_cast<int32_t>(ntohl(ps.x))) / FP_SCALE;
                np.y = static_cast<float>(static_cast<int32_t>(ntohl(ps.y))) / FP_SCALE;
                np.score = ntohs(ps.score);
                np.alive = ps.alive;
                strncpy_s(np.name, sizeof(np.name), ps.name, _TRUNCATE);
                np.active = true;
            }
            for (int i = gs.playerCount; i < MAX_PLAYERS; ++i)
                g_netPlayers[i].active = false;
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

    AESysExit();
    return 0;
}