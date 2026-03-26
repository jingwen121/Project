/* Start Header
*****************************************************************/
/*!
\file server.cpp
\author Harine S/O Sumen (harine.s@digipen.edu)
\par 
\date 22 Feb 2025
\brief
This file implements the server file which will be used to implement a
simple echo server.
Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

/*******************************************************************************
 * A simple TCP/IP server application
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
#include <cstdio>
#include <iostream>			   // cout, cerr
#include <string>			     // string
#include <fstream>
#include <vector>
#include <cstdint>
#include <thread>
#include <mutex>
#include <map>
#include <memory>
#include "taskqueue.h"
#define WINSOCK_VERSION     2
#define WINSOCK_SUBVERSION  2
#define MAX_STR_LEN         1000
#define RETURN_CODE_1       1
#define RETURN_CODE_2       2
#define RETURN_CODE_3       3
#define RETURN_CODE_4       4


#if 0
int main()
{
	// Get Port Number
	std::string portNumber;
	std::cout << "Server Port Number: ";
	std::getline(std::cin, portNumber);

	std::string portString = portNumber;

	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------

	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.
	WSADATA wsaData{};

	// Initialize Winsock. You must call WSACleanup when you are finished.
	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.
	int errorCode = WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}

	// -------------------------------------------------------------------------
	// Resolve own host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------
	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	// For UDP use SOCK_DGRAM instead of SOCK_STREAM.
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 for autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP
	// Create a passive socket that is suitable for bind() and listen().
	hints.ai_flags = AI_PASSIVE;
	
	char host[MAX_STR_LEN];
	gethostname(host, MAX_STR_LEN);

	addrinfo* info = nullptr;
	errorCode = getaddrinfo(host, portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	/* PRINT SERVER IP ADDRESS AND PORT NUMBER */
	char serverIPAddr[MAX_STR_LEN];
	struct sockaddr_in* serverAddress = reinterpret_cast<struct sockaddr_in*> (info->ai_addr);
	inet_ntop(AF_INET, &(serverAddress->sin_addr), serverIPAddr, INET_ADDRSTRLEN);
	getnameinfo(info->ai_addr, static_cast <socklen_t> (info->ai_addrlen), serverIPAddr, sizeof(serverIPAddr), nullptr, 0, NI_NUMERICHOST);
	{
		std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
		std::cout << std::endl;
		std::cout << "Server IP Address: " << serverIPAddr << std::endl;
		std::cout << "Server Port Number: " << portString << std::endl;
	}
	
	// -------------------------------------------------------------------------
	// Create a socket and bind it to own network interface controller.
	//
	// socket()
	// bind()
	// -------------------------------------------------------------------------

	SOCKET listenerSocket = socket(
		hints.ai_family,
		hints.ai_socktype,
		hints.ai_protocol);
	if (INVALID_SOCKET == listenerSocket)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return RETURN_CODE_1;
	}

	errorCode = bind(
		listenerSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (NO_ERROR != errorCode)
	{
		std::cerr << "bind() failed." << std::endl;
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}

	freeaddrinfo(info);

	if (INVALID_SOCKET == listenerSocket)
	{
		std::cerr << "bind() failed." << std::endl;
		WSACleanup();
		return RETURN_CODE_2;
	}


	// -------------------------------------------------------------------------
	// Set a socket in a listening mode and accept 1 incoming client.
	//
	// listen()
	// accept()
	// -------------------------------------------------------------------------

	errorCode = listen(listenerSocket, SOMAXCONN);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "listen() failed." << std::endl;
		closesocket(listenerSocket);
		WSACleanup();
		return RETURN_CODE_3;
	}

	sockaddr clientAddress{};
	SecureZeroMemory(&clientAddress, sizeof(clientAddress));
	int clientAddressSize = sizeof(clientAddress);
	SOCKET clientSocket = accept(
		listenerSocket,
		&clientAddress,
		&clientAddressSize);
	if (INVALID_SOCKET == clientSocket)
	{
		std::cerr << "accept() failed." << std::endl;
		closesocket(listenerSocket);
		WSACleanup();
		return RETURN_CODE_4;
	}

	/* PRINT CLIENT IP ADDRESS AND PORT NUMBER */
	char clientIPAddr[MAX_STR_LEN];
	char clientPort[MAX_STR_LEN];
	getpeername(clientSocket, &clientAddress , &clientAddressSize);
	getnameinfo(&clientAddress , clientAddressSize, clientIPAddr, sizeof(clientIPAddr), clientPort, sizeof(clientPort), NI_NUMERICHOST);
	std::cout << std::endl;
	std::cout << "Client IP Address: " << clientIPAddr << std::endl;
	std::cout << "Client Port Number: " << clientPort << std::endl;
	std::cout << "Connection established! Press 'Enter' to quit -";
	std::getc(stdin);

	// -------------------------------------------------------------------------
	// Shut down and close sockets.
	//
	// shutdown()
	// closesocket()
	// -------------------------------------------------------------------------

	shutdown(clientSocket, SD_BOTH);
	shutdown(listenerSocket, SD_BOTH);

	closesocket(clientSocket);
	closesocket(listenerSocket);


	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------
	
	WSACleanup();
}
#endif

namespace
{
	// Command identifiers used by the server-side application protocol.
	constexpr uint8_t CMD_UNKNOWN = 0x00;
	constexpr uint8_t CMD_REQ_QUIT = 0x01;
	constexpr uint8_t CMD_REQ_ECHO = 0x02;
	constexpr uint8_t CMD_RSP_ECHO = 0x03;
	constexpr uint8_t CMD_REQ_LISTUSERS = 0x04;
	constexpr uint8_t CMD_RSP_LISTUSERS = 0x05;
	constexpr uint8_t CMD_TEST = 0x20;
	constexpr uint8_t CMD_ECHO_ERROR = 0x30;
	constexpr std::size_t MAX_MESSAGE_SIZE = 1024 * 1024;

	struct ClientInfo
	{
		SOCKET socket;
		uint32_t ipNetwork;
		uint16_t portNetwork;
		std::mutex sendMutex;
	};

	constexpr int WORKER_THREAD_COUNT = 10;

	std::mutex g_clientsMutex;
	std::map<std::uint64_t, std::shared_ptr<ClientInfo>> g_clients;

	// Combines IP address and port into a 64-bit key used for client lookups.
	std::uint64_t MakeKey(uint32_t ipNetwork, uint16_t portNetwork)
	{
		return (static_cast<std::uint64_t>(ipNetwork) << 16) | static_cast<std::uint64_t>(portNetwork);
	}

	// Sends all bytes in the given buffer over the socket, retrying until complete or error.
	bool SendAll(SOCKET socket, const char* data, int length)
	{
		if (length <= 0)
		{
			return true;
		}

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

	// Receives a complete echo payload (IP, port, text length and text) from a client socket.
	bool ReceiveEchoPayload(SOCKET socket, uint32_t& ipNetwork, uint16_t& portNetwork, std::vector<char>& textBuffer, uint32_t& textLengthNetwork)
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
		if (lengthHost > MAX_MESSAGE_SIZE)
		{
			return false;
		}

		textBuffer.resize(lengthHost);
		if (lengthHost > 0)
		{
			if (!ReceiveAll(socket, textBuffer.data(), static_cast<int>(lengthHost)))
			{
				return false;
			}
		}

		ipNetwork = ipField;
		portNetwork = portField;
		textLengthNetwork = lengthField;

		return true;
	}

	// Sends a formatted echo or response message to the specified destination client.
	bool SendEchoMessageToClient(const std::shared_ptr<ClientInfo>& client, uint8_t commandId, uint32_t ipNetwork, uint16_t portNetwork, const std::vector<char>& textBuffer, uint32_t textLengthNetwork)
	{
		const uint32_t lengthHost = ntohl(textLengthNetwork);
		std::vector<char> buffer(1 + 4 + 2 + 4 + lengthHost);
		buffer[0] = static_cast<char>(commandId);
		std::memcpy(buffer.data() + 1, &ipNetwork, 4);
		std::memcpy(buffer.data() + 5, &portNetwork, 2);
		std::memcpy(buffer.data() + 7, &textLengthNetwork, 4);
		if (lengthHost > 0)
		{
			std::memcpy(buffer.data() + 11, textBuffer.data(), lengthHost);
		}

		std::lock_guard<std::mutex> lock(client->sendMutex);
		return SendAll(client->socket, buffer.data(), static_cast<int>(buffer.size()));
	}

	// Sends an ECHO_ERROR command to indicate that the server could not process a request.
	void SendEchoError(SOCKET socket)
	{
		uint8_t commandId = CMD_ECHO_ERROR;
		SendAll(socket, reinterpret_cast<const char*>(&commandId), 1);
	}

	// Builds and sends the list of all connected clients to the requesting client.
	void HandleReqListUsers(const std::shared_ptr<ClientInfo>& self)
	{
		std::vector<std::pair<uint32_t, uint16_t>> users;
		{
			std::lock_guard<std::mutex> lock(g_clientsMutex);
			for (const auto& entry : g_clients)
			{
				const auto& client = entry.second;
				users.emplace_back(client->ipNetwork, client->portNetwork);
			}
		}

		uint8_t commandId = CMD_RSP_LISTUSERS;
		uint16_t countHost = static_cast<uint16_t>(users.size());
		uint16_t countNetwork = htons(countHost);

		std::vector<char> buffer;
		buffer.reserve(1 + 2 + users.size() * 6);
		buffer.push_back(static_cast<char>(commandId));
		buffer.insert(buffer.end(), reinterpret_cast<char*>(&countNetwork), reinterpret_cast<char*>(&countNetwork) + 2);

		for (const auto& user : users)
		{
			uint32_t ipNetwork = user.first;
			uint16_t portNetwork = user.second;
			buffer.insert(buffer.end(), reinterpret_cast<char*>(&ipNetwork), reinterpret_cast<char*>(&ipNetwork) + 4);
			buffer.insert(buffer.end(), reinterpret_cast<char*>(&portNetwork), reinterpret_cast<char*>(&portNetwork) + 2);
		}

		std::lock_guard<std::mutex> lock(self->sendMutex);
		SendAll(self->socket, buffer.data(), static_cast<int>(buffer.size()));
	}

	// Main per-client loop that receives commands and performs the required actions.
	void HandleClientConnection(const std::shared_ptr<ClientInfo>& self)
	{
		while (true)
		{
			uint8_t commandId = CMD_UNKNOWN;
			int result = recv(self->socket, reinterpret_cast<char*>(&commandId), 1, 0);
			if (result == 0)
			{
				{
					std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
					std::cerr << "Graceful shutdown." << std::endl;
				}
				break;
			}
			if (result == SOCKET_ERROR)
			{
				{
					std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
					std::cerr << "recv() failed." << std::endl;
				}
				break;
			}

			if (commandId == CMD_REQ_QUIT)
			{
				break;
			}
			else if (commandId == CMD_REQ_LISTUSERS)
			{
				HandleReqListUsers(self);
			}
			else if (commandId == CMD_REQ_ECHO || commandId == CMD_RSP_ECHO)
			{
				uint32_t ipNetwork = 0;
				uint16_t portNetwork = 0;
				uint32_t textLengthNetwork = 0;
				std::vector<char> textBuffer;

				if (!ReceiveEchoPayload(self->socket, ipNetwork, portNetwork, textBuffer, textLengthNetwork))
				{
					SendEchoError(self->socket);
					break;
				}

				{
					char ipString[INET_ADDRSTRLEN]{};
					in_addr addr{};
					addr.S_un.S_addr = ipNetwork;
					const char* result = inet_ntop(AF_INET, &addr, ipString, sizeof(ipString));
					uint16_t portHost = ntohs(portNetwork);

					std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
					std::cout << "==========RECV START==========" << std::endl;
					if (result)
					{
						std::cout << ipString << ":" << portHost << std::endl;
					}
					std::cout.write(textBuffer.data(), static_cast<std::streamsize>(textBuffer.size()));
					std::cout << std::endl;
					std::cout << "==========RECV END==========" << std::endl;
				}

				std::shared_ptr<ClientInfo> destinationClient;
				{
					std::lock_guard<std::mutex> lock(g_clientsMutex);
					auto it = g_clients.find(MakeKey(ipNetwork, portNetwork));
					if (it != g_clients.end())
					{
						destinationClient = it->second;
					}
				}

				if (!destinationClient)
				{
					SendEchoError(self->socket);
					continue;
				}

				uint32_t rewrittenIp = self->ipNetwork;
				uint16_t rewrittenPort = self->portNetwork;

				if (!SendEchoMessageToClient(destinationClient, commandId, rewrittenIp, rewrittenPort, textBuffer, textLengthNetwork))
				{
					SendEchoError(self->socket);
				}
			}
			else
			{
				SendEchoError(self->socket);
				break;
			}
		}

		{
			std::lock_guard<std::mutex> lock(g_clientsMutex);
			g_clients.erase(MakeKey(self->ipNetwork, self->portNetwork));
		}

		shutdown(self->socket, SD_BOTH);
		closesocket(self->socket);
	}
	
	// TaskQueue worker entry point that processes a single client connection.
	bool ExecuteClient(const std::shared_ptr<ClientInfo>& client)
	{
		HandleClientConnection(client);
		return true;
	}
}

// Entry point for the server application. Starts the listener socket and
// dispatches client connections to the worker thread pool.
int main(int argc, char** argv)
{
	std::string portNumber;
	std::string bindAddress = "0.0.0.0";

	if (argc == 2)
	{
		std::ifstream scriptFile(argv[1]);
		if (!scriptFile)
		{
			std::cerr << "Cannot open script file." << std::endl;
			return RETURN_CODE_1;
		}

		if (!std::getline(scriptFile, portNumber))
		{
			std::cerr << "Script file missing port line." << std::endl;
			return RETURN_CODE_1;
		}

		std::string line;
		if (std::getline(scriptFile, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			if (!line.empty())
			{
				bindAddress = line;
			}
		}
	}
	else
	{
		std::cout << "Server Port Number: ";
		std::getline(std::cin, portNumber);
	}

	std::string portString = portNumber;

	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------

	WSADATA wsaData{};

	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.
	int errorCode = WSAStartup(MAKEWORD(WINSOCK_VERSION, WINSOCK_SUBVERSION), &wsaData);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "WSAStartup() failed." << std::endl;
		return errorCode;
	}

	{
		std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
		std::cout
			<< "Winsock version: "
			<< static_cast<int>(LOBYTE(wsaData.wVersion))
			<< "."
			<< static_cast<int>(HIBYTE(wsaData.wVersion))
			<< "\n"
			<< std::endl;
	}

	// Initialize Winsock. You must call WSACleanup when you are finished.
	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.

	// -------------------------------------------------------------------------
	// Resolve own host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	addrinfo* info = nullptr;

	const char* node = nullptr;
	if (argc == 2)
	{
		node = bindAddress.c_str();
	}
	else
	{
		node = nullptr;
	}

	errorCode = getaddrinfo(node, portString.c_str(), &hints, &info);
	if ((NO_ERROR != errorCode) || (nullptr == info))
	{
		std::cerr << "getaddrinfo() failed." << std::endl;
		WSACleanup();
		return errorCode;
	}

	char host[MAX_STR_LEN];
	gethostname(host, MAX_STR_LEN);

	addrinfo displayHints{};
	SecureZeroMemory(&displayHints, sizeof(displayHints));
	displayHints.ai_family = AF_INET;

	addrinfo* displayInfo = nullptr;
	int displayError = getaddrinfo(host, nullptr, &displayHints, &displayInfo);

	char serverIPAddr[MAX_STR_LEN] = "0.0.0.0";
	if ((displayError == 0) && (displayInfo != nullptr))
	{
		getnameinfo(displayInfo->ai_addr, static_cast<socklen_t>(displayInfo->ai_addrlen),
			serverIPAddr, sizeof(serverIPAddr),
			nullptr, 0, NI_NUMERICHOST);
		freeaddrinfo(displayInfo);
	}

	std::cout << std::endl;
	std::cout << "Server IP Address: " << serverIPAddr << std::endl;
	std::cout << "Server Port Number: " << portString << std::endl;

	// -------------------------------------------------------------------------
	// Create a socket and bind it to own network interface controller.
	//
	// socket()
	// bind()
	// -------------------------------------------------------------------------

	SOCKET listenerSocket = socket(
		hints.ai_family,
		hints.ai_socktype,
		hints.ai_protocol);
	if (INVALID_SOCKET == listenerSocket)
	{
		std::cerr << "socket() failed." << std::endl;
		freeaddrinfo(info);
		WSACleanup();
		return RETURN_CODE_1;
	}

	errorCode = bind(
		listenerSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (NO_ERROR != errorCode)
	{
		std::cerr << "bind() failed." << std::endl;
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}

	freeaddrinfo(info);

	if (INVALID_SOCKET == listenerSocket)
	{
		std::cerr << "bind() failed." << std::endl;
		WSACleanup();
		return RETURN_CODE_2;
	}

	// -------------------------------------------------------------------------
	// Set a socket in a listening mode and accept incoming clients.
	//
	// listen()
	// accept()
	// -------------------------------------------------------------------------

	errorCode = listen(listenerSocket, SOMAXCONN);
	if (NO_ERROR != errorCode)
	{
		std::cerr << "listen() failed." << std::endl;
		closesocket(listenerSocket);
		WSACleanup();
		return RETURN_CODE_3;
	}

	const auto onDisconnect = [&]() {
		if (listenerSocket != INVALID_SOCKET)
		{
			shutdown(listenerSocket, SD_BOTH);
			closesocket(listenerSocket);
			listenerSocket = INVALID_SOCKET;
		}
	};

	auto taskQueue = TaskQueue<std::shared_ptr<ClientInfo>, decltype(ExecuteClient), decltype(onDisconnect)>
	{
		static_cast<std::size_t>(WORKER_THREAD_COUNT),
		20,
		ExecuteClient,
		onDisconnect
	};

	while (listenerSocket != INVALID_SOCKET)
	{
		sockaddr_in clientAddress{};
		int clientAddressSize = sizeof(clientAddress);
		SOCKET clientSocket = accept(
			listenerSocket,
			reinterpret_cast<sockaddr*>(&clientAddress),
			&clientAddressSize);
		if (INVALID_SOCKET == clientSocket)
		{
			std::cerr << "accept() failed." << std::endl;
			break;
		}

		uint32_t ipNetwork = clientAddress.sin_addr.S_un.S_addr;
		uint16_t portNetwork = clientAddress.sin_port;

		char clientIPAddr[MAX_STR_LEN];
		char clientPort[MAX_STR_LEN];
		getnameinfo(reinterpret_cast<sockaddr*>(&clientAddress), clientAddressSize, clientIPAddr, sizeof(clientIPAddr), clientPort, sizeof(clientPort), NI_NUMERICHOST);
		{
			std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
			std::cout << std::endl;
			std::cout << "Client IP Address: " << clientIPAddr << std::endl;
			std::cout << "Client Port Number: " << clientPort << std::endl;
		}

		auto clientInfo = std::make_shared<ClientInfo>();
		clientInfo->socket = clientSocket;
		clientInfo->ipNetwork = ipNetwork;
		clientInfo->portNetwork = portNetwork;

		{
			std::lock_guard<std::mutex> lock(g_clientsMutex);
			g_clients[MakeKey(ipNetwork, portNetwork)] = clientInfo;
		}

		taskQueue.produce(clientInfo);
	}

	// -------------------------------------------------------------------------
	// Shut down and close listening socket.
	//
	// shutdown()
	// closesocket()
	// -------------------------------------------------------------------------

	if (listenerSocket != INVALID_SOCKET)
	{
		shutdown(listenerSocket, SD_BOTH);
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}

	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------

	WSACleanup();
}
