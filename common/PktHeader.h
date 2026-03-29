common game_protocol.h
/* Start Header ***************************************************************/
/*!
\file game_protocol.h
...
*/
#ifndef GAME_PROTOCOL_H
#define GAME_PROTOCOL_H

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <WinSock2.h>
  #include <WS2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR   (-1)
  #define closesocket    close
#endif

#include <cstdint>
#include <cstring>

// ?? Networking helpers ???????????????????????????????????????????????????????
inline bool InitNetworking() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}
inline void CleanupNetworking() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// ?? Port / sizing constants ??????????????????????????????????????????????????
constexpr uint16_t GAME_PORT          = 9000; // default UDP port (overridable via config)
constexpr uint16_t GAME_PORT_CLIENT   = 9001; // each client also binds its own port
constexpr int      MAX_PLAYERS        = 6;    // assignment allows groups of 1-6
constexpr int      MAX_ASTEROIDS      = 32;
constexpr int      MAX_BULLETS        = 64;
constexpr int      LEADERBOARD_SIZE   = 5;

// ?? Message type IDs ?????????????????????????????????????????????????????????
enum MsgType : uint8_t {
    // ?? Client ? Server ??????????????????????????????????????????????????????
    MSG_JOIN        = 0x01, // client requests to join
    MSG_INPUT       = 0x02, // player input (keys held this frame)
    MSG_ACK         = 0x03, // generic acknowledgement
    MSG_CHAT        = 0x04, // text chat message
    MSG_QUIT        = 0x05, // client is leaving

    // ?? Server ? Client ??????????????????????????????????????????????????????
    MSG_JOIN_ACK    = 0x10, // server accepts join, assigns player ID
    MSG_JOIN_DENY   = 0x11, // server rejects join (full / wrong state)
    MSG_GAME_STATE  = 0x12, // full authoritative game snapshot
    MSG_ASTEROID_SPAWN  = 0x13, // new asteroid (server-authoritative position)
    MSG_ASTEROID_DEAD   = 0x14, // asteroid destroyed – requires ACK (lock-step)
    MSG_BULLET_SPAWN    = 0x15, // bullet fired
    MSG_PLAYER_DEAD     = 0x16, // a player's ship was destroyed
    MSG_PLAYER_JOIN     = 0x17, // another player joined (broadcast)
    MSG_PLAYER_LEAVE    = 0x18, // a player left (broadcast)
    MSG_SCORE_UPDATE    = 0x19, // live score table
    MSG_ROUND_OVER      = 0x1A, // round ended – carries final scores
    MSG_CHAT_BROADCAST  = 0x1B, // server relays chat to all
    MSG_PING        = 0x1C, // round-trip latency probe
    MSG_PONG        = 0x1D,
    MSG_REJOIN_ACK  = 0x1E, // server sends full state to a rejoining client
};

// ?? Reliability sequence numbers ?????????????????????????????????????????????
//  Only critical events (asteroid death, round-over) use the seq/ack scheme.
//  Regular state updates are fire-and-forget (most-recent wins).
constexpr uint16_t SEQ_UNRELIABLE = 0xFFFF; // marker: no ACK expected

// ????????????????????????????????????????????????????????????????????????????
//  Packet structures  (all packed – no padding bytes)
// ????????????????????????????????????????????????????????????????????????????
#pragma pack(push, 1)

struct PktHeader {
    uint8_t  type;       // MsgType
    uint8_t  playerId;   // sender's player ID (0 = server)
    uint16_t seq;        // sequence number (network byte order); SEQ_UNRELIABLE if n/a
    uint32_t timestamp;  // sender's tick counter (network byte order) – for ordering
};

struct PktJoin {
    PktHeader hdr;
    char      name[16]; // null-terminated player name
};

struct PktJoinAck {
    PktHeader hdr;
    uint8_t   assignedId;         // player ID assigned by server (1-based)
    uint8_t   currentPlayerCount; // how many are already connected
    uint16_t  tickRate;           // server tick rate in Hz (network byte order)
    char      names[MAX_PLAYERS][16]; // names of all current players (slots 0..count-1)
};

// ?? MSG_INPUT ????????????????????????????????????????????????????????????????
constexpr uint8_t INPUT_ROTATE_LEFT  = 0x01;
constexpr uint8_t INPUT_ROTATE_RIGHT = 0x02;
constexpr uint8_t INPUT_THRUST       = 0x04;
constexpr uint8_t INPUT_FIRE         = 0x08;

struct PktInput {
    PktHeader hdr;
    uint8_t   keys; // bitmask of INPUT_* flags
};

struct PktAck {
    PktHeader hdr;
    uint16_t  ackSeq; // the seq being acknowledged (network byte order)
};

// Add playerId to PlayerState so clients can map server entries reliably.
struct PlayerState {
    uint8_t  playerId;  // server-assigned player id (1-based)
    int32_t  x;          // fixed-point position * 1000  (network byte order)
    int32_t  y;
    int16_t  angle;      // degrees * 10  (network byte order)
    int16_t  velX;       // velocity * 1000
    int16_t  velY;
    uint16_t score;      // current round score  (network byte order)
    uint8_t  alive;      // 1 = alive, 0 = dead/respawning
    uint8_t  lives;
    char     name[16];
};

struct AsteroidState {
    uint8_t  id;
    int32_t  x;
    int32_t  y;
    int16_t  velX;
    int16_t  velY;
    uint8_t  size; // 3=large, 2=medium, 1=small
    uint8_t  alive;
};

struct BulletState {
    uint8_t  id;
    uint8_t  ownerId;
    int32_t  x;
    int32_t  y;
    int16_t  velX;
    int16_t  velY;
    uint8_t  alive;
};

struct PktGameState {
    PktHeader    hdr;
    uint32_t     gameTick;                      // server tick (network byte order)
    uint8_t      playerCount;
    uint8_t      asteroidCount;
    uint8_t      bulletCount;
    PlayerState  players[MAX_PLAYERS];
    AsteroidState asteroids[MAX_ASTEROIDS];
    BulletState  bullets[MAX_BULLETS];
};

struct PktAsteroidSpawn {
    PktHeader    hdr;
    AsteroidState ast; // seq in hdr is reliable – client must ACK
};

struct PktAsteroidDead {
    PktHeader hdr;       // seq != SEQ_UNRELIABLE ? client must send MSG_ACK
    uint8_t   asteroidId;
    uint8_t   killerPlayerId;
    uint16_t  points;    // points awarded (network byte order)
};

struct PktBulletSpawn {
    PktHeader   hdr;
    BulletState bullet;
};

struct PktPlayerDead {
    PktHeader hdr;
    uint8_t   deadPlayerId;
    uint8_t   livesRemaining;
};

struct PktPlayerEvent {
    PktHeader hdr;
    uint8_t   eventPlayerId;
    char      name[16];
};

struct PktScoreUpdate {
    PktHeader hdr;
    uint8_t   count;
    struct Entry {
        uint8_t  playerId;
        uint16_t score;   // network byte order
        char     name[16];
    } entries[MAX_PLAYERS];
};

struct LeaderboardEntry {
    char     name[16];
    uint16_t score;
    char     datetime[20]; // "YYYY-MM-DD HH:MM:SS"
};

struct PktRoundOver {
    PktHeader       hdr;          // seq != SEQ_UNRELIABLE – clients must ACK
    uint8_t         winnerCount;
    PktScoreUpdate::Entry winners[MAX_PLAYERS]; // sorted by score descending
    LeaderboardEntry leaderboard[LEADERBOARD_SIZE]; // top-5 all-time
    uint8_t          leaderboardCount;
};

struct PktChat {
    PktHeader hdr;
    char      text[128]; // null-terminated
};

struct PktPing {
    PktHeader hdr;
    uint64_t  clientTime; // echoed back in PONG (network byte order)
};

#pragma pack(pop)

// config examples...
#endif // GAME_PROTOCOL_H