/* Start Header
*****************************************************************/
/*!
\file client.cpp
\author Harine S/O Sumen (harine.s@digipen.edu)
\par 
\date 22 Feb 2025
\brief
This file implements the client file which will be used to implement a 
simple echo server.
Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/


/*******************************************************************************
 * A simple TCP/IP client application
 ******************************************************************************/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"		// Entire Win32 API...
#include "winsock2.h"		// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()

// Tell the Visual Studio linker to include the following library in linking.
// Alternatively, we could add this file to the linker command-line parameters,
// but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")

#include <iostream>			// cout, cerr
#include <string>			// string
#include <fstream>
#include <vector>
#include <cstdint>
#include <cctype>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

#define WINSOCK_VERSION     2
#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1000
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4

namespace
{
	// Command identifiers used in the application protocol between client and server.
	constexpr uint8_t CMD_UNKNOWN = 0x00;
	constexpr uint8_t CMD_REQ_QUIT = 0x01;
	constexpr uint8_t CMD_REQ_ECHO = 0x02;
	constexpr uint8_t CMD_RSP_ECHO = 0x03;
	constexpr uint8_t CMD_REQ_LISTUSERS = 0x04;
	constexpr uint8_t CMD_RSP_LISTUSERS = 0x05;
	constexpr uint8_t CMD_TEST = 0x20;
	constexpr uint8_t CMD_ECHO_ERROR = 0x30;

	std::atomic<bool> g_running{ false };
	std::atomic<bool> g_userQuit{ false };
	std::mutex g_printMutex;
	std::mutex g_sendMutex;

	// Sends all bytes in the given buffer over the socket, retrying until complete or error.
	bool SendAll(SOCKET socket, const char* data, int length)
	{
		std::lock_guard<std::mutex> lock(g_sendMutex);

		int totalSent = 0;
		while (totalSent < length)
		{
			const int sent = send(socket, data + totalSent, length - totalSent, 0);
			if (sent == SOCKET_ERROR)
			{
				return false;
			}
			if (sent == 0)
			{
				return false;
			}
			totalSent += sent;
		}
		return true;
	}

	// Receives exactly length bytes from the socket into the buffer, unless an error occurs.
	bool ReceiveAll(SOCKET socket, char* data, int length)
	{
		int totalReceived = 0;
		while (totalReceived < length)
		{
			const int received = recv(socket, data + totalReceived, length - totalReceived, 0);
			if (received == SOCKET_ERROR)
			{
				return false;
			}
			if (received == 0)
			{
				return false;
			}
			totalReceived += received;
		}
		return true;
	}

	// Returns true if the given line contains only whitespace characters.
	bool IsBlankLine(const std::string& line)
	{
		for (unsigned char ch : line)
		{
			if (!std::isspace(ch))
			{
				return false;
			}
		}
		return true;
	}

	// Parses a string of hexadecimal characters into raw bytes for the /t testing command.
	bool ParseHexBytes(const std::string& text, std::vector<char>& bytes)
	{
		std::string hex;
		for (unsigned char ch : text)
		{
			if (!std::isspace(ch))
			{
				hex.push_back(static_cast<char>(ch));
			}
		}

		if (hex.size() % 2 != 0)
		{
			return false;
		}

		const auto HexValue = [](char c) -> int
		{
			if (c >= '0' && c <= '9')
			{
				return c - '0';
			}
			if (c >= 'a' && c <= 'f')
			{
				return 10 + (c - 'a');
			}
			if (c >= 'A' && c <= 'F')
			{
				return 10 + (c - 'A');
			}
			return -1;
		};

		bytes.clear();
		bytes.reserve(hex.size() / 2);

		for (std::size_t i = 0; i < hex.size(); i += 2)
		{
			const int hi = HexValue(hex[i]);
			const int lo = HexValue(hex[i + 1]);
			if (hi < 0 || lo < 0)
			{
				return false;
			}
			const char value = static_cast<char>((hi << 4) | lo);
			bytes.push_back(value);
		}

		return true;
	}

	bool ReceiveAndPrintMessage(SOCKET socket)
	{
		return false;
	}

	// Receives a complete echo payload (IP, port, text length and text) from the server.
	bool ReceiveEchoPayload(SOCKET socket, uint32_t& ipNetwork, uint16_t& portNetwork, std::string& text)
	{
		uint32_t ipField = 0;
		uint16_t portField = 0;
		uint32_t lengthField = 0;

		if (!ReceiveAll(socket, reinterpret_cast<char*>(&ipField), 4))
		{
			return false;
		}
		if (!ReceiveAll(socket, reinterpret_cast<char*>(&portField), 2))
		{
			return false;
		}
		if (!ReceiveAll(socket, reinterpret_cast<char*>(&lengthField), 4))
		{
			return false;
		}

		uint32_t lengthHost = ntohl(lengthField);
		if (lengthHost > 1024 * 1024)
		{
			return false;
		}

		std::vector<char> buffer(lengthHost);
		if (lengthHost > 0)
		{
			if (!ReceiveAll(socket, buffer.data(), static_cast<int>(lengthHost)))
			{
				return false;
			}
		}

		ipNetwork = ipField;
		portNetwork = portField;
		text.assign(buffer.begin(), buffer.end());

		return true;
	}

	// Constructs and sends an echo message with the specified command and payload fields.
	bool SendEchoMessage(SOCKET socket, uint8_t commandId, uint32_t ipNetwork, uint16_t portNetwork, const std::string& text)
	{
		const uint32_t lengthHost = static_cast<uint32_t>(text.size());
		const uint32_t lengthNetwork = htonl(lengthHost);

		std::vector<char> buffer(1 + 4 + 2 + 4 + lengthHost);
		buffer[0] = static_cast<char>(commandId);
		std::memcpy(buffer.data() + 1, &ipNetwork, 4);
		std::memcpy(buffer.data() + 5, &portNetwork, 2);
		std::memcpy(buffer.data() + 7, &lengthNetwork, 4);
		if (lengthHost > 0)
		{
			std::memcpy(buffer.data() + 11, text.data(), lengthHost);
		}

		return SendAll(socket, buffer.data(), static_cast<int>(buffer.size()));
	}

	// Prints the remote peer address and message text in the standard RECV block format.
	void PrintPeerText(uint32_t ipNetwork, uint16_t portNetwork, const std::string& text)
	{
		char ipString[INET_ADDRSTRLEN]{};
		in_addr addr{};
		addr.S_un.S_addr = ipNetwork;
		const char* result = inet_ntop(AF_INET, &addr, ipString, sizeof(ipString));
		uint16_t portHost = ntohs(portNetwork);

		std::lock_guard<std::mutex> lock(g_printMutex);
		std::cout << "==========RECV START==========" << std::endl;
		if (result)
		{
			std::cout << ipString << ":" << portHost << std::endl;
		}
		std::cout << text << std::endl;
		std::cout << "==========RECV END==========" << std::endl;
	}

	// Handles an incoming REQ_ECHO by printing the text and replying with a RSP_ECHO.
	void HandleIncomingReqEcho(SOCKET socket)
	{
		uint32_t ipNetwork = 0;
		uint16_t portNetwork = 0;
		std::string text;
		if (!ReceiveEchoPayload(socket, ipNetwork, portNetwork, text))
		{
			g_running = false;
			return;
		}

		PrintPeerText(ipNetwork, portNetwork, text);

		SendEchoMessage(socket, CMD_RSP_ECHO, ipNetwork, portNetwork, text);
	}

	// Handles an incoming RSP_ECHO by printing the text from the replying client.
	void HandleIncomingRspEcho(SOCKET socket)
	{
		uint32_t ipNetwork = 0;
		uint16_t portNetwork = 0;
		std::string text;
		if (!ReceiveEchoPayload(socket, ipNetwork, portNetwork, text))
		{
			g_running = false;
			return;
		}

		PrintPeerText(ipNetwork, portNetwork, text);
	}

	// Handles an incoming RSP_LISTUSERS by printing the list of online users.
	void HandleIncomingListUsers(SOCKET socket)
	{
		uint16_t countNetwork = 0;
		if (!ReceiveAll(socket, reinterpret_cast<char*>(&countNetwork), 2))
		{
			g_running = false;
			return;
		}

		uint16_t countHost = ntohs(countNetwork);
		std::vector<std::pair<uint32_t, uint16_t>> users;
		users.reserve(countHost);

		for (uint16_t i = 0; i < countHost; ++i)
		{
			uint32_t ipNetwork = 0;
			uint16_t portNetwork = 0;

			if (!ReceiveAll(socket, reinterpret_cast<char*>(&ipNetwork), 4))
			{
				g_running = false;
				return;
			}
			if (!ReceiveAll(socket, reinterpret_cast<char*>(&portNetwork), 2))
			{
				g_running = false;
				return;
			}

			users.emplace_back(ipNetwork, portNetwork);
		}

		std::lock_guard<std::mutex> lock(g_printMutex);
		std::cout << "==========RECV START==========" << std::endl;
		std::cout << "Users:" << std::endl;
		for (const auto& user : users)
		{
			char ipString[INET_ADDRSTRLEN]{};
			in_addr addr{};
			addr.S_un.S_addr = user.first;
			const char* result = inet_ntop(AF_INET, &addr, ipString, sizeof(ipString));
			uint16_t portHost = ntohs(user.second);
			if (result)
			{
				std::cout << ipString << ":" << portHost << std::endl;
			}
		}
		std::cout << "==========RECV END==========" << std::endl;
	}

	// Handles an incoming ECHO_ERROR command from the server.
	void HandleIncomingEchoError()
	{
		std::lock_guard<std::mutex> lock(g_printMutex);
		std::cout << "==========RECV START==========" << std::endl;
		std::cout << "Echo error" << std::endl;
		std::cout << "==========RECV END==========" << std::endl;
	}

	// Background loop that continuously receives and dispatches commands from the server.
	void ReceiveLoop(SOCKET socket)
	{
		while (g_running)
		{
			uint8_t commandId = CMD_UNKNOWN;
			int result = recv(socket, reinterpret_cast<char*>(&commandId), 1, 0);
			if (result == 0)
			{
				g_running = false;
				break;
			}
			if (result == SOCKET_ERROR)
			{
				g_running = false;
				break;
			}

			if (commandId == CMD_REQ_ECHO)
			{
				HandleIncomingReqEcho(socket);
			}
			else if (commandId == CMD_RSP_ECHO)
			{
				HandleIncomingRspEcho(socket);
			}
			else if (commandId == CMD_RSP_LISTUSERS)
			{
				HandleIncomingListUsers(socket);
			}
			else if (commandId == CMD_ECHO_ERROR)
			{
				HandleIncomingEchoError();
			}
			else if (commandId == CMD_REQ_QUIT)
			{
				g_running = false;
				break;
			}
			else
			{
				g_running = false;
				break;
			}
		}
	}

	// Parses a single line of user input and sends the corresponding protocol command.
	bool ProcessUserInput(SOCKET socket, const std::string& line)
	{
		if (IsBlankLine(line))
		{
			return true;
		}

		if (line == "/q")
		{
			uint8_t command = CMD_REQ_QUIT;
			if (!SendAll(socket, reinterpret_cast<const char*>(&command), 1))
			{
				std::cerr << "Failed to send QUIT command." << std::endl;
			}

			g_userQuit = true;
			g_running = false;
			return false;
		}

		if (line.size() >= 2 && line[0] == '/' && line[1] == 't')
		{
			std::string hexText = line.substr(2);
			std::vector<char> rawBytes;
			if (!ParseHexBytes(hexText, rawBytes))
			{
				std::cerr << "Invalid hex string for /t command." << std::endl;
				return true;
			}

			if (!rawBytes.empty())
			{
				if (!SendAll(socket, rawBytes.data(), static_cast<int>(rawBytes.size())))
				{
					std::cerr << "Failed to send raw bytes." << std::endl;
					return false;
				}
			}

			return true;
		}
		if (line.size() >= 2 && line[0] == '/' && line[1] == 'e')
		{
			std::string rest = line.substr(2);
			if (!rest.empty() && rest[0] == ' ')
			{
				rest.erase(rest.begin());
			}

			std::size_t spacePos = rest.find(' ');
			if (spacePos == std::string::npos)
			{
				return true;
			}

			std::string addressPart = rest.substr(0, spacePos);
			std::string messageText = rest.substr(spacePos + 1);

			std::size_t colonPos = addressPart.find(':');
			if (colonPos == std::string::npos)
			{
				return true;
			}

			std::string ipString = addressPart.substr(0, colonPos);
			std::string portString = addressPart.substr(colonPos + 1);

			in_addr addr{};
			if (inet_pton(AF_INET, ipString.c_str(), &addr) != 1)
			{
				return true;
			}

			int portInt = std::stoi(portString);
			if (portInt <= 0 || portInt > 65535)
			{
				return true;
			}

			uint32_t ipNetwork = addr.S_un.S_addr;
			uint16_t portNetwork = htons(static_cast<uint16_t>(portInt));

			if (!SendEchoMessage(socket, CMD_REQ_ECHO, ipNetwork, portNetwork, messageText))
			{
				std::cerr << "Failed to send REQ_ECHO message." << std::endl;
				return false;
			}

			return true;
		}

		if (line == "/l")
		{
			uint8_t command = CMD_REQ_LISTUSERS;
			if (!SendAll(socket, reinterpret_cast<const char*>(&command), 1))
			{
				std::cerr << "Failed to send REQ_LISTUSERS command." << std::endl;
				return false;
			}

			return true;
		}

		return true;
	}
}

// Entry point for the client application. Connects to the server and processes
// user input or script commands until termination.
int main(int argc, char** argv)
{
	std::string host;
	std::string portNumber;
	std::vector<std::string> scriptLines;
	bool scriptMode = false;

	if (argc == 2)
	{
		scriptMode = true;
		std::ifstream scriptFile(argv[1]);
		if (!scriptFile)
		{
			std::cerr << "Cannot open script file." << std::endl;
			return RETURN_CODE_1;
		}

		if (!std::getline(scriptFile, host))
		{
			std::cerr << "Script file missing host line." << std::endl;
			return RETURN_CODE_1;
		}

		if (!std::getline(scriptFile, portNumber))
		{
			std::cerr << "Script file missing port line." << std::endl;
			return RETURN_CODE_1;
		}

		std::string line;
		while (std::getline(scriptFile, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			if (!IsBlankLine(line))
			{
				scriptLines.push_back(line);
			}
		}
	}
	else
	{
		std::cout << "Server IP Address: ";
		std::getline(std::cin, host);

		std::cout << std::endl;

		std::cout << "Server Port Number: ";
		std::getline(std::cin, portNumber);

		std::cout << std::endl;
	}

	std::string portString = portNumber;

	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------
	WSADATA wsaData{};
	SecureZeroMemory(&wsaData, sizeof(wsaData));

	int errorCode = WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}

	// -------------------------------------------------------------------------
	// Resolve a server host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------
	// -------------------------------------------------------------------------
	// Resolve a server host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* info = nullptr;
	errorCode = getaddrinfo(host.c_str(), portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	// -------------------------------------------------------------------------
	// Create a socket and attempt to connect to the first resolved address.
	//
	// socket()
	// connect()
	// -------------------------------------------------------------------------
	// -------------------------------------------------------------------------
	// Create a socket and attempt to connect to the first resolved address.
	//
	// socket()
	// connect()
	// -------------------------------------------------------------------------
	SOCKET clientSocket = socket(
		info->ai_family,
		info->ai_socktype,
		info->ai_protocol);
	if (INVALID_SOCKET == clientSocket)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return RETURN_CODE_2;
	}

	errorCode = connect(
		clientSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "connect() failed." << std::endl;
		freeaddrinfo(info);
		closesocket(clientSocket);
		WSACleanup();
		return RETURN_CODE_3;
	}

	freeaddrinfo(info);

	g_running = true;
	std::thread receiveThread(ReceiveLoop, clientSocket);

	if (scriptMode)
	{
		for (const std::string& line : scriptLines)
		{
			if (!ProcessUserInput(clientSocket, line))
			{
				break;
			}
#ifdef DEBUG_ASSIGNMENT3_TEST
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(5000ms);
#endif
		}
	}
	else
	{
		std::string line;
		while (true)
		{
			if (!std::getline(std::cin, line))
			{
				break;
			}

			if (!ProcessUserInput(clientSocket, line))
			{
				break;
			}
#ifdef DEBUG_ASSIGNMENT3_TEST
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(5000ms);
#endif
		}
	}

	g_running = false;
	if (g_userQuit)
	{
		std::cout << "disconnection..." << std::endl;
	}

	errorCode = shutdown(clientSocket, SD_BOTH);
	if (SOCKET_ERROR == errorCode)
	{
		std::cerr << "shutdown() failed." << std::endl;
	}

	if (receiveThread.joinable())
	{
		receiveThread.join();
	}

	closesocket(clientSocket);

	WSACleanup();
}
