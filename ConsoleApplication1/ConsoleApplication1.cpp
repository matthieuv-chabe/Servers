// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>

#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <functional>

#include <string>
#include <map>
#include <sstream>

#pragma comment (lib, "Ws2_32.lib")

#define DEFAULT_BUFLEN 4096
#define DEFAULT_PORT "27015"

class TcpServer
{

private:

	WSADATA wsaData;
	int iResult;

	SOCKET ListenSocket = INVALID_SOCKET;

	addrinfo* result = NULL;
	addrinfo hints;

	int iSendResult;
	char recvbuf[DEFAULT_BUFLEN];
	int recvbuflen = DEFAULT_BUFLEN;

	std::vector<SOCKET> sockets;				// List of all sockets
	std::thread AcceptThread;					// The thread that accepts new connections
	std::vector<std::thread> ListenerThread;	// One thread per connection

public:

	// Callbacks
	std::vector<std::function<void(SOCKET)>> cbOnConnect;
	std::vector<std::function<void(SOCKET)>> cbOnDisconnect;
	std::vector<std::function<void(SOCKET, const char* /* message */, size_t /* message len */)>> cbOnRecv;

public:

	TcpServer()
	{
	}

	void StartServer(const std::string_view&& port)
	{
		iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (iResult)
		{
			throw std::exception();
		}

		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		iResult = getaddrinfo(NULL, port.data(), &hints, &result);
		if (iResult)
		{
			WSACleanup();
			throw std::exception();
		}

		// Create a SOCKET for the server to listen for client connections.
		ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (ListenSocket == INVALID_SOCKET)
		{
			printf("socket failed with error: %ld\n", WSAGetLastError());
			freeaddrinfo(result);
			WSACleanup();
			throw std::exception();
		}

		// Setup the TCP listening socket
		iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
		if (iResult == SOCKET_ERROR)
		{
			printf("bind failed with error: %d\n", WSAGetLastError());
			freeaddrinfo(result);
			closesocket(ListenSocket);
			WSACleanup();
			throw std::exception();
		}

		freeaddrinfo(result);

		iResult = listen(ListenSocket, SOMAXCONN);
		if (iResult == SOCKET_ERROR)
		{
			printf("listen failed with error: %d\n", WSAGetLastError());
			closesocket(ListenSocket);
			WSACleanup();
			throw std::exception();
		}

		AcceptThread = std::thread(std::bind(&TcpServer::AcceptThreadFx, this));
		AcceptThread.detach();
	}

private:

	void AcceptThreadFx()
	{
		while (true)
		{
			auto client_socket = accept(this->ListenSocket, (sockaddr*)NULL, (int*)NULL);
			if (client_socket == INVALID_SOCKET)
			{
				throw std::exception();
				WSACleanup();
			}

			this->sockets.push_back(client_socket);

			for (const auto f : cbOnConnect)
				f(client_socket);

			std::thread listener_thread(std::bind(&TcpServer::ListenerThreadFx, this, client_socket));
			listener_thread.detach();
		}
	}

	void ListenerThreadFx(SOCKET client_socket)
	{
		while (true)
		{
			iResult = recv(client_socket, recvbuf, recvbuflen, 0);

			if (iResult > 0)
			{
				// std::cout << "Recv " << iResult << " bytes" << std::endl;
				for (const auto &f : cbOnRecv)
					f(client_socket, recvbuf, iResult);
			}
			else if (iResult == 0)
			{
				for (const auto f : cbOnDisconnect)
					f(client_socket);
				
				std::cout << "connected" << std::endl;

				this->sockets.erase(std::remove(sockets.begin(), sockets.end(), client_socket), sockets.end());
				break;
			}
			else
			{
				for (const auto f : cbOnDisconnect)
					f(client_socket);

				std::cout << "disconnected" << std::endl;

				this->sockets.erase(std::remove(sockets.begin(), sockets.end(), client_socket), sockets.end());
				break;

				std::cout << iResult << " " << WSAGetLastError() << std::endl;
			}

		}

	}

public:

	void Broadcast(const std::string message)
	{
		Broadcast(message.c_str(), message.size()+1);
	}

	void Broadcast(const char* message, const size_t size)
	{
		for (const auto s : sockets)
		{
			Send(s, message, size);
		}
	}

	static void Send(SOCKET s, std::string message)
	{
		std::cout << "sending " << message << std::endl;
		Send(s, message.c_str(), message.size() + 1);
	}

	static void Send(SOCKET s, const char* message, const size_t size)
	{
		const auto iSendResult = send(s, message, size, MSG_OOB);
		if (iSendResult != size)
		{
			std::cout << "Sending fail :" << WSAGetLastError() << std::endl;
			closesocket(s);
		}
	}

	static void Close(SOCKET s)
	{
		std::cout << "closing" << std::endl;
		shutdown(s, SD_BOTH);
	}

};

class HttpServer
{
public:

	struct HttpRequest {
		enum RequestType { GET, POST, UPDATE, UNK };

		SOCKET s;
		TcpServer *tcp;

		// Header frequently used stuff
		size_t contentlength{ 0 };
		
		RequestType type;
		std::string destination;
		std::map<std::string, std::string> headers {{"request",""}};
		std::string content;
	};

	enum HttpParsingState {
		IN_HEADER,
		END_OF_HEADERS,
		IN_BODY,
	};

	TcpServer tcp;

	std::vector<std::function<void(HttpRequest& req)>> cbOnHttpRequestDone;

	void StartServer(const char* port)
	{
		using namespace std::placeholders;
		tcp.cbOnRecv.push_back(std::bind(&HttpServer::OnTcpMsgReceived, this, _1, _2, _3));

		tcp.StartServer(port);
	}

	std::map<SOCKET, HttpParsingState> message_parsing_state;
	std::map<SOCKET, std::string> message_parts;
	std::map<SOCKET, HttpRequest> message_request;

	void HandleHttpHeaderLine(SOCKET& socket, const std::string &&line)
	{
		std::cout << __FUNCTION__ << line << std::endl;

		if (line.size() < 3) return;
		
		auto& mr = message_request[socket];

		// message_request[socket].headers.append(line).append("\r\n");
		const auto idx = line.find_first_of(':');

		// For the "GET /" line that doesnt contain ":" char
		if (idx == std::string::npos) [[unlikely]]
		{

			int offset = 0;
			switch (line[0])
			{
			case 'G':
				mr.type = HttpRequest::RequestType::GET;
				offset = 4; // +1 for the space between the keyword and the path
				break;
			case 'P':
				mr.type = HttpRequest::RequestType::POST;
				offset = 5;
				break;
			default:
				mr.type = HttpRequest::RequestType::UNK;
				offset = line.find_first_of(' ');
			}

			auto otherspace = line.find_first_of(' ', offset + 1);
			mr.destination = line.substr(offset, otherspace - offset);

			mr.headers["request"] = line;
			return;
		}

		const auto key = line.substr(0, idx);
		const auto value = line.substr(idx + 1);

		if (key == "Content-Length") [[unlikely]]
		{
			mr.contentlength = atoi(value.c_str());
		}
		else [[likely]]	// If no particular handler, store it in headers directly
		{
			mr.headers[key] = value;
		}
	}

	bool OnHttpHeaderMessageReceived(SOCKET socket, std::string &data)
	{

		std::cout << __FUNCTION__ << data << std::endl;
		
		if (data.size() < 2) return false;

		auto datasize = data.size();
		for (auto i = 1; i < datasize; ++i)
		{
			if (data[i] != '\n') [[likely]] continue;

			HandleHttpHeaderLine(socket, data.substr(0, i - 1)); // Skip the \r before \n. There's ALWAYS a \r before.
			data = data.substr(i + 1);

			datasize -= (i + 1);

			if (datasize > 1 && data[1] == '\n') [[unlikely]]  // TODO : if the first \r\n and this one are in separate packets, it wont trigger.
			{
				// std::cout << "Done" << std::endl;
				data = data.substr(2); // skip \r\n
				return true;
			}

			i = 0; // Restart the loop
		}
		return false;
	}

	void OnTcpMsgReceived(SOCKET socket, const char *data, size_t len)
	{
		// std::cout << "recv tcp msg" << std::endl;
		auto msg = &message_parts[socket];
		msg->append(data, len);

		// std::cout << "Cur message: " << std::endl << *msg << std::endl;

		begin_of_switch:
		switch (message_parsing_state[socket])
		{
			case IN_HEADER:
			{
				if (OnHttpHeaderMessageReceived(socket, *msg))	// If done parsing
				{
					message_parsing_state[socket] = END_OF_HEADERS;
					goto begin_of_switch;
				}
				break;  
			}

			case END_OF_HEADERS:
				// std::cout << "sending answer" << std::endl;
				// tcp.Send(socket, "HTTP/1.1 204 No Content \r\nServer-Timing: matthieu;dur=10, thomas;dur=200.2\r\n\r\n");

				if (message_request[socket].contentlength != 0 && message_request[socket].contentlength > msg->size() + 1)
				{
					std::cout << "Body not received in total. Waiting. " << msg->size() << "/" << message_request[socket].contentlength << std::endl;
				}

				message_request[socket].content = *msg;
				
				for (const auto& f : cbOnHttpRequestDone)
					f(message_request[socket]);

				break;

			case IN_BODY:
				std::cout << ".";
				break;
		}

	}

};

class WebSocketServer
{
public:
	inline static const std::string WS_GUID { "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" };
};

class endpoint
{
public:

	SOCKET s;
	sockaddr_in sockaddrin = { 0 };

	endpoint(SOCKET in) : s(in) {}

	SOCKET data() const
	{
		return s;
	}

	const std::string Ip()
	{
		if (sockaddrin.sin_port == 0) GrabSockaddrinData();

		const auto& x = sockaddrin.sin_addr.S_un.S_un_b;

		std::ostringstream ss;
		ss << (int)x.s_b1 << "." << (int)x.s_b2 << "." << (int)x.s_b3 << "." << (int)x.s_b4;

		return ss.str();
	}

	const uint16_t Port()
	{
		if (sockaddrin.sin_port == 0) GrabSockaddrinData();

		return sockaddrin.sin_port;
	}

	void GrabSockaddrinData()
	{
		int addrlen = sizeof(sockaddrin);
		getsockname(s, (sockaddr*)&sockaddrin, &addrlen);
	}
};

class WebServer
{
	
	using HttpRequestCallback_t = bool(HttpServer::HttpRequest&);

public:

	HttpServer http;
	std::map<std::string_view, std::function<HttpRequestCallback_t>> Gets, Posts, Reqs;

	struct WebRequest
	{
		HttpServer::HttpRequest http;
	};

	void StartServer(const char* port)
	{
		using namespace std::placeholders;
		http.cbOnHttpRequestDone.push_back(std::bind(&WebServer::HandleHttpRequest, this, _1));
		http.tcp.cbOnConnect.push_back([this](SOCKET s) { http.message_request[s].tcp = &this->http.tcp; http.message_request[s].s = s; });

		http.StartServer(port);
	}

	void HandleHttpRequest(HttpServer::HttpRequest &httpReq)
	{
		endpoint ep(httpReq.s);
		std::cout << "Received HTTP request from " << ep.Ip() << ":" << ep.Port() << std::endl;

		if (httpReq.type == HttpServer::HttpRequest::RequestType::GET) [[likely]]
		{
			std::cout << "get" << std::endl;
			httpReq.tcp->Send(httpReq.s, "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello worssssssssssssssssssssssssssssssld!"); // mdlol
		}
		else if (httpReq.type == HttpServer::HttpRequest::RequestType::POST)
		{
			std::cout << "post: " << httpReq.content << std::endl;
			httpReq.tcp->Send(httpReq.s, "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello worssssssssssssssssssssssssssssssld!");
			// httpReq.tcp->Close(httpReq.s);
		}
	}

	void Get(const std::string_view path, std::function<HttpRequestCallback_t>&& callback)
	{
		Gets[path] = callback;
	}

};



int main()
{
	WebServer ts;
	ts.StartServer("81");

	while (true)
	{
		std::cin.get();
	}
}
