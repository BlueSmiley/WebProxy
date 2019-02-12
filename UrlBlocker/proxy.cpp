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
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "27015"
#define DEFAULT_BUFLEN 512

int chunkString(std::string str,char* buf,int buflen) {
	int endbuflen = str.size() < buflen ? str.size() : buflen;
	str.copy(buf, endbuflen);
	return endbuflen;
}

int forward_connection(SOCKET ClientSocket, std::string request, std::string hostname,
	std::string protocol, comms* messagePasser, cache* shared_cache, bool dont_cache) {
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



	std::string response = "";
	bool cacheExists = false;
	int bandwidth = 0;
	auto start = std::chrono::high_resolution_clock::now();

	//Check cache for result
	{
		std::lock_guard<std::mutex> g(shared_cache->mutex);
		std::unordered_map<std::string, std::string> cache = shared_cache->cache;
		auto cachedData = cache.find(hostname);
		if (cachedData != cache.end()) {
			response = cachedData -> second;
			cacheExists = true;
			//printf("\n\nKido riddo mido!\n\n");
		}
	}

	if (!cacheExists) {
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
		bandwidth += iResult;

		// shutdown the connection for sending since no more data will be sent
		// the client can still use the ConnectSocket for receiving data
		iResult = shutdown(ConnectSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			printf("shutdown failed: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			return 1;
		}

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

			bandwidth += iResult;
			response += std::string(recvbuf, iResult);
		} while (iResult > 0);

	}
		
	auto end = std::chrono::high_resolution_clock::now();
	auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();


	//Print the response and time taken to management console
	{
		std::lock_guard<std::mutex> g(messagePasser->mutex);
		messagePasser->queue.push("\n Time taken = " + std::to_string(time) + "\nBandwidth used = " 
			+ std::to_string(bandwidth));
		messagePasser->queue.push(response);
		messagePasser->status = true;
		messagePasser->condVar.notify_all();
	}

	//conditionally update cache
	if(!cacheExists)
	{

		std::lock_guard<std::mutex> g(shared_cache->mutex);
		shared_cache->cache[hostname] = response;
		//printf("Cache exists...search for it, I left it all at 'that place'!\n");
	}

	int iSendResult;
	do
	{
		iResult = chunkString(response, recvbuf, recvbuflen);
		// Send the result from server to client
		iSendResult = send(ClientSocket, recvbuf, iResult, 0);
		if (iSendResult == SOCKET_ERROR) {
			printf("send failed: %d\n", WSAGetLastError());
			closesocket(ClientSocket);
			return 1;
		}
		response = response.substr(iResult);
	} while (iResult > 0);

	//printf("Bytes sent: %d\n", iSendResult);

	// shutdown the send conn to client
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

/*	Handles all interaction with the header of the initial client request
	Only does any network interaction of closing client socket if the requested url is banned.
	All other network interaction left up to children and parent functions
	Also calls appropriate function to handle any further required interactions
*/
int parseHttpHeader(SOCKET ClientSocket, std::smatch packetInfo, std::string request, comms* messagePasser,
	cache* shared_cache) {
	//Pass header of message to be printed on management console
	{
			std::lock_guard<std::mutex> g(messagePasser->mutex);
			messagePasser->queue.push(request);
			messagePasser->status = true;
			messagePasser->condVar.notify_all();
	}
	std::string header = packetInfo.prefix().str();
	std::string frst_field = header.substr(0, header.find("\r\n"));
	std::string request_type = split(frst_field, " ")[0];
	std::string host_field = header.substr(header.find("Host"), header.find("\r\n"));
	std::string hostname = split(split(host_field, " ")[1],"\r\n")[0];
	std::string port_alias = "http";
	bool websock = false;
	bool dont_cache = false;
	int err_status = -1;

	//In case the Host is specified as an addresss and port instead
	std::vector<std::string> full_path = split(hostname, ":");
	if (full_path.size() == 2) {
		hostname = full_path[0];
		port_alias = full_path[1];
	}
	//In case the path is given as an IPv6 address and Port instead
	else if (full_path.size() > 2 && hostname.find("[") != std::string::npos
		&& hostname.find("]") != std::string::npos) {
		hostname = hostname.substr(hostname.find("[")+1, hostname.find("]")-1);
		port_alias = hostname.substr(hostname.find("]") + 1);
	}

	bool isBanned = false;
	{
		std::lock_guard<std::mutex> g(messagePasser->mutex);
		for (const auto& url : messagePasser->banned_urls) {
			if (url == hostname) {
				messagePasser->queue.push("Banned url:" + hostname);
				messagePasser->status = true;
				messagePasser->condVar.notify_all();
				isBanned = true;
			}
		}
	}
	if (isBanned) {
		int iResult;
		// shutdown the send conn to client
		iResult = shutdown(ClientSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			printf("shutdown failed: %d\n", WSAGetLastError());
		}

		// cleanup
		closesocket(ClientSocket);
		return 1;
	}

	if (header.find("Upgrade: WebSocket") != std::string::npos) {
		websock = true;
	}

	if (request_type == "GET") {
		err_status = forward_connection(ClientSocket, request, hostname, port_alias, 
			messagePasser, shared_cache,dont_cache);
	}
	else if (request_type == "CONNECT") {
		
	}
	//For all other types of requests just do a simple no cache request transfer
	else {
		dont_cache = true;
	}

	/*Debug printing
	{
		std::lock_guard<std::mutex> g(messagePasser->mutex);
		messagePasser->queue.push(hostname);
		messagePasser->queue.push(port_alias);
		messagePasser->queue.push(request_type);
		messagePasser->status = true;
		messagePasser->condVar.notify_all();
	}
	*/
	return err_status;
}

/*	receives initial request from client and then passes the request for processing 
*/
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
						return parseHttpHeader(ClientSocket, header_match, response, messagePasser, shared_cache);
					}
					else {
						//debug code ... usually means more data still expected
						//printf("Packet data:%d vs expected length:%d",
						//	header_match.suffix().length(), content_length);
					}
				}
				else {
					// no Content length and end of header indicates no body..I think
					return parseHttpHeader(ClientSocket, header_match, response, messagePasser, shared_cache);
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

//Listens to main port and creates new thread to handle any new requests
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