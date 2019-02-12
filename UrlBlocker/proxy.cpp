#include "pch.h"
#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <thread>
#include <regex>
#include "UrlBlocker.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <unordered_map>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "27015"
#define DEFAULT_BUFLEN 512

int forward_connection(SOCKET ClientSocket, std::smatch packetInfo, std::string request, comms* messagePasser,
		cache* shared_cache) {
	int recvbuflen = DEFAULT_BUFLEN;
	char recvbuf[DEFAULT_BUFLEN];

	int iResult;

	SOCKET ConnectSocket = INVALID_SOCKET;
	struct addrinfo *result = NULL,
					*ptr = NULL,
					hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	std::string header = packetInfo.prefix().str();
	std::string frst_field = header.substr(0, header.find("\r\n"));
	std::vector<std::string> components = split(frst_field," ");
	std::vector<std::string> targetHost = split(components[1], "//");
	std::string hostname = targetHost[1].substr(0,targetHost[1].size()-1);
	std::string protocol = targetHost[0].substr(0, targetHost[0].size() - 1);

	//printf("host: %s  protocol: %s \n",hostname.c_str(), protocol.c_str());

	//RAII auto unlock of mutex using lock_gaurd
	{
		std::lock_guard<std::mutex> g(messagePasser->mutex);
		messagePasser->queue.push(request);
		messagePasser->status = true;
		messagePasser->condVar.notify_all();
	}

	bool banStatus = false;
	//RAII auto unlock of mutex using lock_gaurd
	{
		std::lock_guard<std::mutex> g(messagePasser->mutex);
		for (const auto& url : messagePasser->banned_urls) {
			if (url == hostname) {
				messagePasser->queue.push("Banned url:" + hostname);
				messagePasser->status = true;
				messagePasser->condVar.notify_all();
				banStatus = true;
			}
		}
	}


	if (!banStatus) {

		iResult = getaddrinfo(hostname.c_str(), protocol.c_str(), &hints, &result);
		if (iResult != 0) {
			printf("getaddrinfo failed: %d\n", iResult);
			return 1;
		}
		// Attempt to connect to the first address returned by
		// the call to getaddrinfo
		ptr = result;

		// Create a SOCKET for connecting to server
		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
			ptr->ai_protocol);

		if (ConnectSocket == INVALID_SOCKET) {
			printf("Error at socket(): %ld\n", WSAGetLastError());
			freeaddrinfo(result);
			return 1;
		}

		// Connect to server.
		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;
		}

		// Should really try the next address returned by getaddrinfo
		// if the connect call failed
		// But for this simple example we just free the resources
		// returned by getaddrinfo and print an error message

		freeaddrinfo(result);

		if (ConnectSocket == INVALID_SOCKET) {
			printf("Unable to connect to server!\n");
			return 1;
		}

		//todo split up into smaller chunks rather than in one go
		iResult = send(ConnectSocket, request.c_str(), request.size(), 0);
		if (iResult == SOCKET_ERROR) {
			printf("send failed: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			return 1;
		}

		// shutdown the connection for sending since no more data will be sent
		// the client can still use the ConnectSocket for receiving data
		iResult = shutdown(ConnectSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			printf("shutdown failed: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			return 1;
		}

		std::string response = "";
		int iSendResult;
		// Receive data until the server closes the connection
		do {
			iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
			if (iResult > 0)
				printf("Bytes received: %d\n", iResult);
			else if (iResult == 0) {
				printf("Connection closed\n");
			}
			else
				printf("recv failed: %d\n", WSAGetLastError());

			response += std::string(recvbuf, iResult);

			// Send the result from server to client
			iSendResult = send(ClientSocket, recvbuf, iResult, 0);
			if (iSendResult == SOCKET_ERROR) {
				printf("send failed: %d\n", WSAGetLastError());
				closesocket(ClientSocket);
				return 1;
			}

			printf("Bytes sent: %d\n", iSendResult);
		} while (iResult > 0);


		//RAII auto unlock of mutex using lock_gaurd
		{
			std::lock_guard<std::mutex> g(messagePasser->mutex);
			messagePasser->queue.push(response);
			messagePasser->status = true;
			messagePasser->condVar.notify_all();
		}
		// shutdown the send half of the connection since no more data will be sent
		iResult = shutdown(ConnectSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			printf("shutdown failed: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			return 1;
		}

	}

	// shutdown the send half of the connection since no more data will be sent
	iResult = shutdown(ClientSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed: %d\n", WSAGetLastError());
		closesocket(ClientSocket);
		return 1;
	}

	// cleanup
	closesocket(ClientSocket);

	return 0;
}

int connectionHandler(SOCKET ClientSocket, comms* messagePasser, cache* shared_cache) {

	char recvbuf[DEFAULT_BUFLEN];
	int recvbuflen = DEFAULT_BUFLEN;
	int iResult;
	std::string response = "";
	std::regex header_regex("\r\n\r\n");
	std::smatch header_match;
	std::regex regex("Content-Length: \\d\\r\\n");
	std::smatch match;

	// Receive until the peer shuts down the connection
	do {

		iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
		if (iResult > 0) {
			response += std::string(recvbuf,iResult);
			if (std::regex_search(response, header_match, header_regex)) {
				std::string header = header_match.prefix().str();
				if (std::regex_search(header, match, regex)) {
					int content_length = std::stoi(match.str().substr(16, match.str().length()));
					//printf("%d", content_length);
					if (header_match.suffix().length() >= content_length) {
						// Optional garbage data removal code
						int garbageDataLength = header_match.suffix().length() - content_length;
						std::string realReponse = response.substr(0, response.size() - garbageDataLength);
						return forward_connection(ClientSocket, header_match, response, messagePasser, shared_cache);
					}
					else {
						//debug code ... usually means more data still expected
						//printf("Packet data:%d vs expected length:%d",
						//	header_match.suffix().length(), content_length);
					}
				}
				else {
					// no Content length and end of header indicates no body..I think
					return forward_connection(ClientSocket, header_match, response, messagePasser, shared_cache);
				}
			}
		
		}
		else if (iResult == 0)
			printf("Connection closing...\n");
		else {
			printf("recv failed: %d\n", WSAGetLastError());
			closesocket(ClientSocket);
			return 1;
		}

	} while (iResult > 0);
	return 1;
}


int main(){
	comms messagePasser;
	messagePasser.status = false;
	cache shared_cache;
	std::thread commsThread(console, &messagePasser);
	//commsThread.join();

	WSADATA wsaData;
	struct addrinfo *result = NULL, *ptr = NULL, hints;
	int iResult;
	SOCKET ListenSocket = INVALID_SOCKET;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}


	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the server
	iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}
	freeaddrinfo(result);

	if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
		printf("Listen failed with error: %ld\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	while (true) {
		SOCKET ClientSocket = INVALID_SOCKET;

		// Accept a client socket
		ClientSocket = accept(ListenSocket, NULL, NULL);
		if (ClientSocket == INVALID_SOCKET) {
			printf("accept failed: %d\n", WSAGetLastError());
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}
		std::thread t1(connectionHandler,ClientSocket,&messagePasser, &shared_cache);
		t1.detach();
	}
	
	WSACleanup();

	//std::thread t1(process);
	//t1.join();


}