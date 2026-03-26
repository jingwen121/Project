/* Start Header ***************************************************************/
/*!
\file protocol.h

\author TODO

\par email: TODO

\date 10 Mar, 2026

\brief Copyright (C) 2026 DigiPen Institute of Technology.

Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited. */
/* End Header *****************************************************************/
#ifndef PROTOCOL_H
#define PROTOCOL_H

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
  #define INVALID_SOCKET -1
  #define SOCKET_ERROR   -1
  #define closesocket    close
#endif

#include <cstdint>

// ── TCP Command IDs ─────────────────────────────────────────────────────────
enum CMDID : unsigned char {
    UNKNOWN        = 0x00, // not in use
    REQ_QUIT       = 0x01,
    REQ_DOWNLOAD   = 0x02,
    RSP_DOWNLOAD   = 0x03,
    REQ_LISTFILES  = 0x04,
    RSP_LISTFILES  = 0x05,
    CMD_TEST       = 0x20, // not in use
    DOWNLOAD_ERROR = 0x30
};

// ── UDP datagram flags ──────────────────────────────────────────────────────
// LSB = 0 => file data,  LSB = 1 => ACK
constexpr uint8_t UDP_FLAG_DATA = 0x00;
constexpr uint8_t UDP_FLAG_ACK  = 0x01;

// ── UDP datagram header (sent before file data or as ACK) ───────────────────
#pragma pack(push, 1)
struct UDPHeader {
    uint8_t  flags;          // UDP_FLAG_DATA or UDP_FLAG_ACK
    uint32_t sessionId;      // session ID (network byte order)
    uint32_t seqNum;         // sequence number (network byte order)
    uint32_t ackNum;         // acknowledgement number (network byte order)
    uint32_t fileLength;     // total file length (network byte order)
    uint32_t fileOffset;     // offset into file (network byte order)
    uint32_t dataLength;     // length of payload following this header (network byte order)
};
#pragma pack(pop)

// ── Configuration defaults ──────────────────────────────────────────────────
constexpr int    DEFAULT_TCP_PORT      = 4200;
constexpr int    DEFAULT_UDP_PORT      = 4201;
constexpr size_t UDP_PAYLOAD_SIZE      = 1400;   // safe payload per datagram (< MTU)
constexpr size_t UDP_PACKET_SIZE       = sizeof(UDPHeader) + UDP_PAYLOAD_SIZE;
constexpr size_t TCP_BUFFER_SIZE       = 4096;
constexpr int    UDP_TIMEOUT_MS        = 500;     // retransmit timeout
constexpr int    MAX_RETRIES           = 50;      // max retransmits per packet
constexpr int    WINDOW_SIZE           = 16;      // Go-Back-N window size
constexpr int    THREAD_POOL_SIZE      = 8;

// ── Helper: initialise Winsock (Windows only) ───────────────────────────────
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

#endif // PROTOCOL_H
