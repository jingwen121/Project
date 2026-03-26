/* Start Header ***************************************************************/
/*!
\file server_main.cpp

\author TODO

\par email: TODO

\date 10 Mar, 2026

\brief Copyright (C) 2026 DigiPen Institute of Technology.

Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited. */
/* End Header *****************************************************************/

#include "protocol.h"
#include "taskqueue.h"
#include "taskqueue.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

// ── Globals ─────────────────────────────────────────────────────────────────
static std::string  g_ftpPath;          // path to downloadable files
static int          g_tcpPort = 0;
static int          g_udpPort = 0;
static SOCKET       g_tcpListenSock = INVALID_SOCKET;
static SOCKET       g_udpSock       = INVALID_SOCKET;
static TaskQueue    g_threadPool;
static std::atomic<uint32_t> g_nextSessionId{1};
static std::mutex   g_printMtx;

// ── Utility: send all bytes over TCP ────────────────────────────────────────
static bool SendAll(SOCKET sock, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(sock, data + sent, len - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

// ── Utility: receive exactly `len` bytes over TCP ───────────────────────────
static bool RecvAll(SOCKET sock, char* buf, int len) {
    int received = 0;
    while (received < len) {
        int r = recv(sock, buf + received, len - received, 0);
        if (r <= 0) return false;
        received += r;
    }
    return true;
}

// ── Build RSP_LISTFILES message ─────────────────────────────────────────────
static std::vector<char> BuildFileListResponse() {
    std::vector<std::string> files;
    if (fs::exists(g_ftpPath) && fs::is_directory(g_ftpPath)) {
        for (auto& entry : fs::directory_iterator(g_ftpPath)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().filename().string());
            }
        }
    }
    std::sort(files.begin(), files.end());

    // Calculate Length of File List field
    uint32_t listLen = 0;
    for (auto& f : files) {
        listLen += 4 + static_cast<uint32_t>(f.size()); // 4 bytes name len + name
    }

    std::vector<char> msg;
    // CMD ID
    msg.push_back(static_cast<char>(RSP_LISTFILES));
    // Number of files (2 bytes, big-endian)
    uint16_t numFiles = htons(static_cast<uint16_t>(files.size()));
    msg.insert(msg.end(), reinterpret_cast<char*>(&numFiles),
               reinterpret_cast<char*>(&numFiles) + 2);
    // Length of file list (4 bytes, big-endian)
    uint32_t netListLen = htonl(listLen);
    msg.insert(msg.end(), reinterpret_cast<char*>(&netListLen),
               reinterpret_cast<char*>(&netListLen) + 4);
    // For each file: filename length (4) + filename
    for (auto& f : files) {
        uint32_t nameLen = htonl(static_cast<uint32_t>(f.size()));
        msg.insert(msg.end(), reinterpret_cast<char*>(&nameLen),
                   reinterpret_cast<char*>(&nameLen) + 4);
        msg.insert(msg.end(), f.begin(), f.end());
    }
    return msg;
}

// ── Send file over UDP using Go-Back-N ──────────────────────────────────────
static void SendFileUDP(uint32_t sessionId, const std::string& filePath,
                        const sockaddr_in& clientAddr) {
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return;

    uint32_t fileLength = static_cast<uint32_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    std::vector<char> fileData(fileLength);
    if (fileLength > 0) {
        ifs.read(fileData.data(), fileLength);
    }
    ifs.close();

    // Split file into chunks
    uint32_t totalPackets = (fileLength + static_cast<uint32_t>(UDP_PAYLOAD_SIZE) - 1)
                            / static_cast<uint32_t>(UDP_PAYLOAD_SIZE);
    if (fileLength == 0) totalPackets = 1; // send one empty packet for 0-byte files

    uint32_t base = 0;       // oldest unacked packet
    uint32_t nextSeq = 0;    // next packet to send

    // Create the UDP socket for this session (use the shared server UDP socket)
    // We'll use the global g_udpSock

    // Set recv timeout for ACKs
    #ifdef _WIN32
    DWORD timeout = UDP_TIMEOUT_MS;
    #else
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = UDP_TIMEOUT_MS * 1000;
    #endif
    setsockopt(g_udpSock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    auto buildDataPacket = [&](uint32_t seqNum) -> std::vector<char> {
        uint32_t offset = seqNum * static_cast<uint32_t>(UDP_PAYLOAD_SIZE);
        uint32_t dataLen = std::min(static_cast<uint32_t>(UDP_PAYLOAD_SIZE),
                                     fileLength - offset);
        if (fileLength == 0) {
            dataLen = 0;
            offset = 0;
        }

        UDPHeader hdr{};
        hdr.flags      = UDP_FLAG_DATA;
        hdr.sessionId  = htonl(sessionId);
        hdr.seqNum     = htonl(seqNum);
        hdr.ackNum     = 0;
        hdr.fileLength = htonl(fileLength);
        hdr.fileOffset = htonl(offset);
        hdr.dataLength = htonl(dataLen);

        std::vector<char> pkt(sizeof(UDPHeader) + dataLen);
        std::memcpy(pkt.data(), &hdr, sizeof(UDPHeader));
        if (dataLen > 0) {
            std::memcpy(pkt.data() + sizeof(UDPHeader), fileData.data() + offset, dataLen);
        }
        return pkt;
    };

    int retries = 0;
    while (base < totalPackets) {
        // Send packets in the window
        while (nextSeq < totalPackets && nextSeq < base + WINDOW_SIZE) {
            auto pkt = buildDataPacket(nextSeq);
            sendto(g_udpSock, pkt.data(), static_cast<int>(pkt.size()), 0,
                   reinterpret_cast<const sockaddr*>(&clientAddr), sizeof(clientAddr));
            nextSeq++;
        }

        // Wait for ACK
        char ackBuf[sizeof(UDPHeader)];
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int r = recvfrom(g_udpSock, ackBuf, sizeof(ackBuf), 0,
                         reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);

        if (r >= static_cast<int>(sizeof(UDPHeader))) {
            UDPHeader ackHdr;
            std::memcpy(&ackHdr, ackBuf, sizeof(UDPHeader));
            if (ackHdr.flags & UDP_FLAG_ACK) {
                uint32_t ackNum = ntohl(ackHdr.ackNum);
                uint32_t ackSession = ntohl(ackHdr.sessionId);
                if (ackSession == sessionId && ackNum >= base) {
                    base = ackNum + 1;
                    retries = 0;
                }
            }
        } else {
            // Timeout - resend from base
            retries++;
            if (retries > MAX_RETRIES) {
                std::lock_guard<std::mutex> lock(g_printMtx);
                std::cout << "[Server] Session " << sessionId
                          << ": max retries exceeded, aborting.\n";
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_printMtx);
                std::cout << "[Server] Session " << sessionId
                          << ": timeout, resending from seq " << base << "\n";
            }
            nextSeq = base; // Go-Back-N: resend from base
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_printMtx);
        std::cout << "[Server] Session " << sessionId
                  << ": file transfer complete (" << fileLength << " bytes)\n";
    }
}

// ── Handle one TCP client ───────────────────────────────────────────────────
static void HandleClient(SOCKET clientSock) {
    {
        std::lock_guard<std::mutex> lock(g_printMtx);
        std::cout << "[Server] Client connected.\n";
    }

    while (true) {
        // Read Command ID (1 byte)
        char cmdId = 0;
        if (!RecvAll(clientSock, &cmdId, 1)) break;

        switch (static_cast<CMDID>(cmdId)) {
        case REQ_LISTFILES: {
            std::lock_guard<std::mutex> lock(g_printMtx);
            std::cout << "[Server] Received REQ_LISTFILES\n";
            auto resp = BuildFileListResponse();
            SendAll(clientSock, resp.data(), static_cast<int>(resp.size()));
            break;
        }

        case REQ_DOWNLOAD: {
            // Read IP (4) + Port (2) + FilenameLen (4) + Filename (var)
            char ipBytes[4];
            char portBytes[2];
            char lenBytes[4];
            if (!RecvAll(clientSock, ipBytes, 4)) goto disconnect;
            if (!RecvAll(clientSock, portBytes, 2)) goto disconnect;
            if (!RecvAll(clientSock, lenBytes, 4)) goto disconnect;

            uint32_t filenameLen;
            std::memcpy(&filenameLen, lenBytes, 4);
            filenameLen = ntohl(filenameLen);

            std::string filename(filenameLen, '\0');
            if (filenameLen > 0) {
                if (!RecvAll(clientSock, &filename[0], filenameLen)) goto disconnect;
            }

            // Client UDP address
            sockaddr_in clientUdpAddr{};
            clientUdpAddr.sin_family = AF_INET;
            std::memcpy(&clientUdpAddr.sin_addr, ipBytes, 4);
            uint16_t clientUdpPort;
            std::memcpy(&clientUdpPort, portBytes, 2);
            clientUdpAddr.sin_port = clientUdpPort; // already in network byte order

            {
                std::lock_guard<std::mutex> lock(g_printMtx);
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientUdpAddr.sin_addr, ipStr, sizeof(ipStr));
                std::cout << "[Server] REQ_DOWNLOAD: " << filename
                          << " to " << ipStr << ":" << ntohs(clientUdpPort) << "\n";
            }

            // Check if file exists
            std::string fullPath = g_ftpPath + "/" + filename;
            if (!fs::exists(fullPath)) {
                char errCmd = static_cast<char>(DOWNLOAD_ERROR);
                SendAll(clientSock, &errCmd, 1);
                std::lock_guard<std::mutex> lock(g_printMtx);
                std::cout << "[Server] File not found: " << fullPath << "\n";
                break;
            }

            uint32_t fileLen = static_cast<uint32_t>(fs::file_size(fullPath));
            uint32_t sessionId = g_nextSessionId.fetch_add(1);

            // Build RSP_DOWNLOAD: CMDID(1) + IP(4) + Port(2) + SessionID(4) + FileLen(4)
            std::vector<char> rsp(1 + 4 + 2 + 4 + 4);
            rsp[0] = static_cast<char>(RSP_DOWNLOAD);

            // Server IP - get local address of the TCP socket
            sockaddr_in localAddr{};
            int addrLen = sizeof(localAddr);
            getsockname(clientSock, reinterpret_cast<sockaddr*>(&localAddr), &addrLen);
            std::memcpy(&rsp[1], &localAddr.sin_addr, 4);

            // Server UDP port (network byte order)
            uint16_t netUdpPort = htons(static_cast<uint16_t>(g_udpPort));
            std::memcpy(&rsp[5], &netUdpPort, 2);

            // Session ID
            uint32_t netSessionId = htonl(sessionId);
            std::memcpy(&rsp[7], &netSessionId, 4);

            // File length
            uint32_t netFileLen = htonl(fileLen);
            std::memcpy(&rsp[11], &netFileLen, 4);

            SendAll(clientSock, rsp.data(), static_cast<int>(rsp.size()));

            // Start file transfer in a new thread
            std::thread(SendFileUDP, sessionId, fullPath, clientUdpAddr).detach();
            break;
        }

        case REQ_QUIT:
            {
                std::lock_guard<std::mutex> lock(g_printMtx);
                std::cout << "[Server] Client sent REQ_QUIT.\n";
            }
            goto disconnect;

        default:
            {
                std::lock_guard<std::mutex> lock(g_printMtx);
                std::cout << "[Server] Unknown command: 0x"
                          << std::hex << (int)(unsigned char)cmdId << std::dec << "\n";
            }
            break;
        }
    }

disconnect:
    closesocket(clientSock);
    std::lock_guard<std::mutex> lock(g_printMtx);
    std::cout << "[Server] Client disconnected.\n";
}

// ── Main ────────────────────────────────────────────────────────────────────
int main() {
    if (!InitNetworking()) {
        std::cerr << "Failed to initialise networking.\n";
        return 1;
    }

    // Prompt for configuration
    std::cout << "Server TCP port Number: ";
    std::cin >> g_tcpPort;
    std::cout << "Server UDP Port Number: ";
    std::cin >> g_udpPort;
    std::cin.ignore();
    std::cout << "Path                  : ";
    std::getline(std::cin, g_ftpPath);

    // Remove trailing slash/backslash
    while (!g_ftpPath.empty() &&
           (g_ftpPath.back() == '/' || g_ftpPath.back() == '\\')) {
        g_ftpPath.pop_back();
    }

    std::cout << "[Server] Starting on TCP:" << g_tcpPort
              << " UDP:" << g_udpPort
              << " Path:" << g_ftpPath << "\n";

    // ── Create TCP listen socket ────────────────────────────────────────
    g_tcpListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_tcpListenSock == INVALID_SOCKET) {
        std::cerr << "Failed to create TCP socket.\n";
        CleanupNetworking();
        return 1;
    }

    // Allow address reuse
    int optval = 1;
    setsockopt(g_tcpListenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));

    sockaddr_in tcpAddr{};
    tcpAddr.sin_family      = AF_INET;
    tcpAddr.sin_addr.s_addr = INADDR_ANY;
    tcpAddr.sin_port        = htons(static_cast<uint16_t>(g_tcpPort));

    if (bind(g_tcpListenSock, reinterpret_cast<sockaddr*>(&tcpAddr), sizeof(tcpAddr)) == SOCKET_ERROR) {
        std::cerr << "TCP bind failed.\n";
        closesocket(g_tcpListenSock);
        CleanupNetworking();
        return 1;
    }

    if (listen(g_tcpListenSock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "TCP listen failed.\n";
        closesocket(g_tcpListenSock);
        CleanupNetworking();
        return 1;
    }

    // ── Create UDP socket ───────────────────────────────────────────────
    g_udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udpSock == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP socket.\n";
        closesocket(g_tcpListenSock);
        CleanupNetworking();
        return 1;
    }

    sockaddr_in udpAddr{};
    udpAddr.sin_family      = AF_INET;
    udpAddr.sin_addr.s_addr = INADDR_ANY;
    udpAddr.sin_port        = htons(static_cast<uint16_t>(g_udpPort));

    if (bind(g_udpSock, reinterpret_cast<sockaddr*>(&udpAddr), sizeof(udpAddr)) == SOCKET_ERROR) {
        std::cerr << "UDP bind failed.\n";
        closesocket(g_tcpListenSock);
        closesocket(g_udpSock);
        CleanupNetworking();
        return 1;
    }

    // ── Start thread pool ───────────────────────────────────────────────
    g_threadPool.Init(THREAD_POOL_SIZE);
    std::cout << "[Server] Listening... (thread pool: " << THREAD_POOL_SIZE << ")\n";

    // ── Accept loop ─────────────────────────────────────────────────────
    while (true) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSock = accept(g_tcpListenSock,
                                   reinterpret_cast<sockaddr*>(&clientAddr),
                                   &clientAddrLen);
        if (clientSock == INVALID_SOCKET) continue;

        g_threadPool.Submit([clientSock]() {
            HandleClient(clientSock);
        });
    }

    // Cleanup (unreachable in normal flow)
    g_threadPool.Shutdown();
    closesocket(g_tcpListenSock);
    closesocket(g_udpSock);
    CleanupNetworking();
    return 0;
}
