/* Start Header ***************************************************************/
/*!
\file client_main.cpp

\author TODO

\par email: TODO

\date 10 Mar, 2026

\brief Copyright (C) 2026 DigiPen Institute of Technology.

Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited. */
/* End Header *****************************************************************/

#include "protocol.h"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <map>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

// ── Globals ─────────────────────────────────────────────────────────────────
static std::string  g_serverIP;
static int          g_serverTcpPort = 0;
static int          g_serverUdpPort = 0;
static int          g_clientUdpPort = 0;
static std::string  g_downloadPath;
static SOCKET       g_tcpSock   = INVALID_SOCKET;
static SOCKET       g_udpSock   = INVALID_SOCKET;
static std::mutex   g_printMtx;
static std::atomic<bool> g_running{true};

// Active download sessions: sessionId -> {fileLength, fileData, receivedFlags}
struct DownloadSession {
    uint32_t             fileLength;
    std::string          filename;
    std::vector<char>    fileData;
    std::vector<bool>    receivedChunks;
    uint32_t             totalPackets;
    std::atomic<bool>    complete{false};
};

static std::mutex g_sessionMtx;
static std::map<uint32_t, std::shared_ptr<DownloadSession>> g_sessions;

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

// ── UDP receiver thread ─────────────────────────────────────────────────────
static void UDPReceiverThread() {
    char buf[UDP_PACKET_SIZE + 256];
    sockaddr_in fromAddr{};

    while (g_running) {
        int fromLen = sizeof(fromAddr);
        int r = recvfrom(g_udpSock, buf, sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);
        if (r < static_cast<int>(sizeof(UDPHeader))) continue;

        UDPHeader hdr;
        std::memcpy(&hdr, buf, sizeof(UDPHeader));

        if (hdr.flags & UDP_FLAG_ACK) continue; // ignore ACKs on client side

        uint32_t sessionId  = ntohl(hdr.sessionId);
        uint32_t seqNum     = ntohl(hdr.seqNum);
        uint32_t fileLength = ntohl(hdr.fileLength);
        uint32_t fileOffset = ntohl(hdr.fileOffset);
        uint32_t dataLength = ntohl(hdr.dataLength);

        // Find or validate session
        std::shared_ptr<DownloadSession> session;
        {
            std::lock_guard<std::mutex> lock(g_sessionMtx);
            auto it = g_sessions.find(sessionId);
            if (it == g_sessions.end()) continue;
            session = it->second;
        }

        // Write data to session buffer
        if (dataLength > 0 && fileOffset + dataLength <= session->fileLength) {
            std::memcpy(session->fileData.data() + fileOffset,
                        buf + sizeof(UDPHeader), dataLength);
            if (seqNum < session->totalPackets) {
                session->receivedChunks[seqNum] = true;
            }
        } else if (fileLength == 0) {
            // 0-byte file
            session->receivedChunks[0] = true;
        }

        // Send ACK
        UDPHeader ack{};
        ack.flags     = UDP_FLAG_ACK;
        ack.sessionId = htonl(sessionId);
        ack.ackNum    = htonl(seqNum);
        ack.seqNum    = 0;
        sendto(g_udpSock, reinterpret_cast<const char*>(&ack), sizeof(UDPHeader), 0,
               reinterpret_cast<const sockaddr*>(&fromAddr), sizeof(fromAddr));

        // Check if download is complete
        bool allReceived = true;
        for (uint32_t i = 0; i < session->totalPackets; ++i) {
            if (!session->receivedChunks[i]) {
                allReceived = false;
                break;
            }
        }

        if (allReceived && !session->complete.exchange(true)) {
            // Write file to disk
            std::string outPath = g_downloadPath + "/" + session->filename;
            fs::create_directories(fs::path(outPath).parent_path());
            std::ofstream ofs(outPath, std::ios::binary);
            if (ofs.is_open()) {
                if (session->fileLength > 0) {
                    ofs.write(session->fileData.data(), session->fileLength);
                }
                ofs.close();
            }

            {
                std::lock_guard<std::mutex> lock(g_printMtx);
                std::cout << "[Client] Download complete: " << session->filename
                          << " (" << session->fileLength << " bytes)\n";
            }

            // Remove session
            {
                std::lock_guard<std::mutex> lock(g_sessionMtx);
                g_sessions.erase(sessionId);
            }
        }
    }
}

// ── TCP receiver thread (T_Commu) ───────────────────────────────────────────
static void TCPReceiverThread() {
    while (g_running) {
        char cmdId = 0;
        if (!RecvAll(g_tcpSock, &cmdId, 1)) {
            std::lock_guard<std::mutex> lock(g_printMtx);
            std::cout << "[Client] Server disconnected.\n";
            g_running = false;
            break;
        }

        switch (static_cast<CMDID>(cmdId)) {
        case RSP_LISTFILES: {
            // Read: NumFiles(2) + ListLen(4) + [NameLen(4) + Name(var)]*N
            char numBuf[2], lenBuf[4];
            if (!RecvAll(g_tcpSock, numBuf, 2)) { g_running = false; return; }
            if (!RecvAll(g_tcpSock, lenBuf, 4)) { g_running = false; return; }

            uint16_t numFiles;
            std::memcpy(&numFiles, numBuf, 2);
            numFiles = ntohs(numFiles);

            uint32_t listLen;
            std::memcpy(&listLen, lenBuf, 4);
            listLen = ntohl(listLen);

            std::lock_guard<std::mutex> lock(g_printMtx);
            std::cout << "[Client] Available files (" << numFiles << "):\n";

            for (uint16_t i = 0; i < numFiles; ++i) {
                char nameLenBuf[4];
                if (!RecvAll(g_tcpSock, nameLenBuf, 4)) { g_running = false; return; }
                uint32_t nameLen;
                std::memcpy(&nameLen, nameLenBuf, 4);
                nameLen = ntohl(nameLen);

                std::string name(nameLen, '\0');
                if (nameLen > 0) {
                    if (!RecvAll(g_tcpSock, &name[0], nameLen)) { g_running = false; return; }
                }
                std::cout << "  " << (i + 1) << ". " << name << "\n";
            }
            break;
        }

        case RSP_DOWNLOAD: {
            // Read: IP(4) + Port(2) + SessionID(4) + FileLen(4)
            char ipBuf[4], portBuf[2], sessBuf[4], flenBuf[4];
            if (!RecvAll(g_tcpSock, ipBuf, 4))   { g_running = false; return; }
            if (!RecvAll(g_tcpSock, portBuf, 2))  { g_running = false; return; }
            if (!RecvAll(g_tcpSock, sessBuf, 4))  { g_running = false; return; }
            if (!RecvAll(g_tcpSock, flenBuf, 4))  { g_running = false; return; }

            uint32_t sessionId, fileLen;
            uint16_t serverUdpPort;
            std::memcpy(&sessionId, sessBuf, 4);
            sessionId = ntohl(sessionId);
            std::memcpy(&fileLen, flenBuf, 4);
            fileLen = ntohl(fileLen);
            std::memcpy(&serverUdpPort, portBuf, 2);
            serverUdpPort = ntohs(serverUdpPort);

            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, ipBuf, ipStr, sizeof(ipStr));

            // Find the pending filename from the session map
            // We stored it when sending REQ_DOWNLOAD
            std::string filename;
            {
                std::lock_guard<std::mutex> lock(g_sessionMtx);
                auto it = g_sessions.find(sessionId);
                if (it != g_sessions.end()) {
                    filename = it->second->filename;
                }
            }

            // Create/update session
            auto session = std::make_shared<DownloadSession>();
            session->fileLength   = fileLen;
            session->filename     = filename;
            session->fileData.resize(fileLen, 0);
            session->totalPackets = (fileLen + static_cast<uint32_t>(UDP_PAYLOAD_SIZE) - 1)
                                    / static_cast<uint32_t>(UDP_PAYLOAD_SIZE);
            if (fileLen == 0) session->totalPackets = 1;
            session->receivedChunks.resize(session->totalPackets, false);

            {
                std::lock_guard<std::mutex> lock(g_sessionMtx);
                g_sessions[sessionId] = session;
            }

            {
                std::lock_guard<std::mutex> lock(g_printMtx);
                std::cout << "[Client] RSP_DOWNLOAD: session=" << sessionId
                          << " file=" << filename
                          << " size=" << fileLen
                          << " from " << ipStr << ":" << serverUdpPort << "\n";
            }
            break;
        }

        case DOWNLOAD_ERROR: {
            std::lock_guard<std::mutex> lock(g_printMtx);
            std::cout << "[Client] DOWNLOAD_ERROR: file not found on server.\n";
            break;
        }

        default: {
            std::lock_guard<std::mutex> lock(g_printMtx);
            std::cout << "[Client] Unknown response: 0x"
                      << std::hex << (int)(unsigned char)cmdId << std::dec << "\n";
            break;
        }
        }
    }
}

// ── Parse /d directive ──────────────────────────────────────────────────────
// Format: /d <IP>:<PORT> <filename>
static bool ParseDownloadCmd(const std::string& input,
                             std::string& ip, uint16_t& port, std::string& filename) {
    // Skip "/d "
    std::string rest = input.substr(3);
    // Find the space between address:port and filename
    size_t spacePos = rest.find(' ');
    if (spacePos == std::string::npos) return false;

    std::string addrPort = rest.substr(0, spacePos);
    filename = rest.substr(spacePos + 1);

    // Split IP:PORT
    size_t colonPos = addrPort.rfind(':');
    if (colonPos == std::string::npos) return false;

    ip = addrPort.substr(0, colonPos);
    port = static_cast<uint16_t>(std::stoi(addrPort.substr(colonPos + 1)));
    return true;
}

// ── Main (T_Main thread) ────────────────────────────────────────────────────
int main() {
    if (!InitNetworking()) {
        std::cerr << "Failed to initialise networking.\n";
        return 1;
    }

    // Prompt for configuration
    std::cout << "Server IP Address     : ";
    std::getline(std::cin, g_serverIP);
    std::cout << "Server TCP Port Number: ";
    std::cin >> g_serverTcpPort;
    std::cout << "Server UDP Port Number: ";
    std::cin >> g_serverUdpPort;
    std::cout << "Client UDP Port Number: ";
    std::cin >> g_clientUdpPort;
    std::cin.ignore();
    std::cout << "Path                  : ";
    std::getline(std::cin, g_downloadPath);

    // Remove trailing slash
    while (!g_downloadPath.empty() &&
           (g_downloadPath.back() == '/' || g_downloadPath.back() == '\\')) {
        g_downloadPath.pop_back();
    }

    // Create download directory if needed
    if (!g_downloadPath.empty()) {
        fs::create_directories(g_downloadPath);
    }

    // ── Connect TCP to server ───────────────────────────────────────────
    g_tcpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_tcpSock == INVALID_SOCKET) {
        std::cerr << "Failed to create TCP socket.\n";
        CleanupNetworking();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(static_cast<uint16_t>(g_serverTcpPort));
    inet_pton(AF_INET, g_serverIP.c_str(), &serverAddr.sin_addr);

    if (connect(g_tcpSock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to server.\n";
        closesocket(g_tcpSock);
        CleanupNetworking();
        return 1;
    }
    std::cout << "[Client] Connected to server.\n";

    // ── Create UDP socket ───────────────────────────────────────────────
    g_udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udpSock == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP socket.\n";
        closesocket(g_tcpSock);
        CleanupNetworking();
        return 1;
    }

    sockaddr_in udpBindAddr{};
    udpBindAddr.sin_family      = AF_INET;
    udpBindAddr.sin_addr.s_addr = INADDR_ANY;
    udpBindAddr.sin_port        = htons(static_cast<uint16_t>(g_clientUdpPort));

    if (bind(g_udpSock, reinterpret_cast<sockaddr*>(&udpBindAddr), sizeof(udpBindAddr)) == SOCKET_ERROR) {
        std::cerr << "UDP bind failed on port " << g_clientUdpPort << ".\n";
        closesocket(g_tcpSock);
        closesocket(g_udpSock);
        CleanupNetworking();
        return 1;
    }

    // Set UDP recv timeout
    #ifdef _WIN32
    DWORD timeout = 1000;
    #else
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    #endif
    setsockopt(g_udpSock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    // ── Start receiver threads ──────────────────────────────────────────
    std::thread tcpThread(TCPReceiverThread);
    std::thread udpThread(UDPReceiverThread);

    // ── Main input loop (T_Main) ────────────────────────────────────────
    static uint32_t nextLocalSessionId = 1;

    while (g_running) {
        std::string input;
        std::getline(std::cin, input);
        if (input.empty()) continue;

        if (input == "/q") {
            char cmd = static_cast<char>(REQ_QUIT);
            SendAll(g_tcpSock, &cmd, 1);
            g_running = false;
            break;
        }

        if (input == "/l") {
            char cmd = static_cast<char>(REQ_LISTFILES);
            SendAll(g_tcpSock, &cmd, 1);
            continue;
        }

        if (input.substr(0, 3) == "/d ") {
            std::string ip;
            uint16_t port;
            std::string filename;
            if (!ParseDownloadCmd(input, ip, port, filename)) {
                std::cout << "[Client] Invalid /d format. Use: /d <IP>:<PORT> <filename>\n";
                continue;
            }

            // Pre-register session with a temporary ID so we can match the filename
            // when RSP_DOWNLOAD arrives. Server assigns the real session ID.
            // We'll store by server-assigned ID in the TCP handler.
            // For now, just send the request.

            // Build REQ_DOWNLOAD: CMDID(1) + IP(4) + Port(2) + FilenameLen(4) + Filename
            std::vector<char> msg;
            msg.push_back(static_cast<char>(REQ_DOWNLOAD));

            // IP address (4 bytes)
            in_addr addr{};
            inet_pton(AF_INET, ip.c_str(), &addr);
            msg.insert(msg.end(), reinterpret_cast<char*>(&addr),
                       reinterpret_cast<char*>(&addr) + 4);

            // Port (2 bytes, network byte order)
            uint16_t netPort = htons(port);
            msg.insert(msg.end(), reinterpret_cast<char*>(&netPort),
                       reinterpret_cast<char*>(&netPort) + 2);

            // Filename length (4 bytes, network byte order)
            uint32_t nameLen = htonl(static_cast<uint32_t>(filename.size()));
            msg.insert(msg.end(), reinterpret_cast<char*>(&nameLen),
                       reinterpret_cast<char*>(&nameLen) + 4);

            // Filename
            msg.insert(msg.end(), filename.begin(), filename.end());

            // We need to store the filename so we can associate it when RSP_DOWNLOAD comes.
            // We'll use a pending download list and match by order.
            // Simpler: store in session map with a placeholder session ID that gets
            // replaced when RSP_DOWNLOAD arrives.
            // Actually, we need to store the filename keyed by the session ID the server
            // will assign. Since we don't know it yet, let's use a pending queue.
            {
                // Store pending filename - will be matched in TCP receiver
                // Use next expected session ID (assumes sequential)
                std::lock_guard<std::mutex> lock(g_sessionMtx);
                auto session = std::make_shared<DownloadSession>();
                session->filename = filename;
                g_sessions[nextLocalSessionId] = session;
                nextLocalSessionId++;
            }

            SendAll(g_tcpSock, msg.data(), static_cast<int>(msg.size()));

            std::lock_guard<std::mutex> lock(g_printMtx);
            std::cout << "[Client] Requested download: " << filename
                      << " via " << ip << ":" << port << "\n";
            continue;
        }

        std::cout << "[Client] Unknown command. Use /l, /d, or /q\n";
    }

    // ── Cleanup ─────────────────────────────────────────────────────────
    g_running = false;
    closesocket(g_tcpSock);
    closesocket(g_udpSock);

    if (tcpThread.joinable()) tcpThread.join();
    if (udpThread.joinable()) udpThread.join();

    CleanupNetworking();
    std::cout << "[Client] Exiting.\n";
    return 0;
}
