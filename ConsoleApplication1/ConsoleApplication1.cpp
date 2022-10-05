// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <iostream>
#include <thread>
#include <vector>
#include <functional>

#include <string>
#include <map>
#include <sstream>

#pragma comment (lib, "Ws2_32.lib")

constexpr auto DEFAULT_BUFLEN = 10;

class tcp_server
{
private:

	WSADATA wsa_data_;

	SOCKET listen_socket_ = INVALID_SOCKET;

	std::vector<SOCKET> sockets_;				// List of all sockets
	std::thread accept_thread_;					// The thread that accepts new connections
	std::vector<std::thread> listener_thread_;	// One thread per connection

public:

	// Callbacks
	std::vector<std::function<void(SOCKET)>> cb_on_connect;
	std::vector<std::function<void(SOCKET)>> cb_on_disconnect;
	std::vector<std::function<void(SOCKET, const char* /* message */, size_t /* message len */)>> cb_on_recv;

public:

	tcp_server()
	{
	}

	void start_server(const char* port)
	{
		auto i_result = WSAStartup(MAKEWORD(2, 2), &wsa_data_);
		if (i_result)
		{
			throw std::exception();
		}

		addrinfo hints;
		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		addrinfo* result;
		i_result = getaddrinfo(nullptr, port, &hints, &result); // pNodeName can be IP to bind to
		if (i_result)
		{
			WSACleanup();
			throw std::exception();
		}

		// Create a SOCKET for the server to listen for client connections.
		listen_socket_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (listen_socket_ == INVALID_SOCKET)
		{
			printf("socket failed with error: %d\n", WSAGetLastError());
			freeaddrinfo(result);
			WSACleanup();
			throw std::exception();
		}

		// Setup the TCP listening socket
		i_result = bind(listen_socket_, result->ai_addr, static_cast<int>(result->ai_addrlen));
		if (i_result == SOCKET_ERROR)
		{
			printf("bind failed with error: %d\n", WSAGetLastError());
			freeaddrinfo(result);
			closesocket(listen_socket_);
			WSACleanup();
			throw std::exception();
		}

		freeaddrinfo(result);

		i_result = listen(listen_socket_, SOMAXCONN);
		if (i_result == SOCKET_ERROR)
		{
			printf("listen failed with error: %d\n", WSAGetLastError());
			closesocket(listen_socket_);
			WSACleanup();
			throw std::exception();
		}

		accept_thread_ = std::thread([this] { accept_thread_fx(); });
		accept_thread_.detach();
	}

private:

	[[noreturn]]
	void accept_thread_fx()
	{
		while (true)
		{
			auto client_socket = accept(this->listen_socket_, nullptr, nullptr);
			if (client_socket == INVALID_SOCKET)
			{
				WSACleanup();
				throw std::exception();
			}

			this->sockets_.push_back(client_socket);

			for (const auto& f : cb_on_connect)
				f(client_socket);

			std::thread listener_thread([this, client_socket] { listener_thread_fx(client_socket); });
			listener_thread.detach();
		}
	}

	void listener_thread_fx(const SOCKET client_socket)
	{

		while (true)
		{
			char recvbuf[DEFAULT_BUFLEN];
			constexpr size_t recvbuflen = DEFAULT_BUFLEN;

			if (const auto i_result = recv(client_socket, recvbuf, recvbuflen, 0); i_result > 0)
			{
				for (const auto& f : cb_on_recv)
					f(client_socket, recvbuf, i_result);
			}
			else if (i_result == 0)
			{
				for (const auto& f : cb_on_disconnect)
					f(client_socket);

				std::cout << "connected" << std::endl;

				this->sockets_.erase(std::ranges::remove(sockets_, client_socket).begin(), sockets_.end());
				break;
			}
			else
			{
				for (const auto f : cb_on_disconnect)
					f(client_socket);

				std::cout << "disconnected" << std::endl;

				this->sockets_.erase(std::ranges::remove(sockets_, client_socket).begin(), sockets_.end());
				break;

				std::cout << i_result << " " << WSAGetLastError() << std::endl;
			}
		}
	}

public:

	void broadcast(const std::string& message) const
	{
		broadcast(message.c_str(), message.size() + 1);
	}

	void broadcast(const char* message, const size_t size) const
	{
		for (const auto s : sockets_)
		{
			send(s, message, size);
		}
	}

	static void send(const SOCKET s, const std::string& message)
	{
		std::cout << "sending " << message << std::endl;
		send(s, message.c_str(), message.size() + 1);
	}

	static void send(const SOCKET s, const char* message, const size_t size)
	{
		if (const auto i_send_result = ::send(s, message, size, MSG_OOB); i_send_result != size) [[unlikely]]
		{
			std::cout << "Sending fail :" << WSAGetLastError() << std::endl;
			closesocket(s);
		}
	}

	static void close(SOCKET s)
	{
		std::cout << "closing" << std::endl;
		shutdown(s, SD_BOTH);
	}
};

class http_server
{
public:

	struct http_request {

		enum request_type { get, post, update, unk };

		SOCKET s;
		tcp_server* tcp;

		// Header frequently used stuff
		size_t contentlength{ 0 };

		request_type type;
		std::string destination;
		std::map<std::string, std::string> headers{ {"request",""} };
		std::string content;
	};

	enum http_parsing_state {
		in_header,
		end_of_headers,
		in_body,
	};

	tcp_server tcp;

	std::vector<std::function<void(http_request& req)>> cbOnHttpRequestDone;

	void start_server(const char* port)
	{
		using namespace std::placeholders;
		tcp.cb_on_recv.emplace_back([this](auto&& ph1, auto&& ph2, auto&& ph3)
			{
				on_tcp_msg_received(
					std::forward<decltype(ph1)>(ph1),
					std::forward<decltype(ph2)>(ph2),
					std::forward<decltype(ph3)>(ph3));
			});

		tcp.start_server(port);
	}

private: // TODO : this one is the correct one lol
public:

	std::map<SOCKET, http_parsing_state> message_parsing_state;
	std::map<SOCKET, std::string> message_parts;
	std::map<SOCKET, http_request> message_request;

private:

	void handle_http_header_line(const SOCKET& socket, const std::string&& line)
	{
		std::cout << __FUNCTION__ << line << std::endl;

		if (line.size() < 3) return;

		auto& mr = message_request[socket];

		// message_request[socket].headers.append(line).append("\r\n");
		const auto idx = line.find_first_of(':');

		// For the "GET /" line that doesnt contain ":" char
		if (idx == std::string::npos) [[unlikely]]
		{
			size_t offset = 0;
			switch (line[0])
			{
			case 'G':
				mr.type = http_request::request_type::get;
				offset = 4; // +1 for the space between the keyword and the path
				break;
			case 'P':
				mr.type = http_request::request_type::post;
				offset = 5;
				break;
			default:
				mr.type = http_request::request_type::unk;
				offset = line.find_first_of(' ');
			}

			const auto otherspace = line.find_first_of(' ', offset + 1);
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

	bool on_http_header_message_received(const SOCKET socket, std::string& data)
	{
		std::cout << __FUNCTION__ << data << std::endl;

		if (data.size() < 2) return false;

		auto datasize = data.size();
		for (auto i = 1; i < datasize; ++i)
		{
			if (data[i] != '\n') [[likely]] continue;

			if (datasize > 2) [[likely]]   // Most of the time, some data will be received for the header
			{
				handle_http_header_line(socket, data.substr(0, i - 1)); // Skip the \r before \n. There's ALWAYS a \r before.
				data = data.substr(i + 1);
				datasize -= (i + 1);
			}

				if (datasize > 1 && data[1] == '\n') [[unlikely]]
				{
					data = data.substr(2); // skip \r\n
					return true;
				}

			i = 0; // Restart the loop
		}
		return false;
	}

	void on_tcp_msg_received(const SOCKET socket, const char* data, size_t len)
	{
		const auto msg = &message_parts[socket];
		msg->append(data, len);

	begin_of_switch:
		switch (message_parsing_state[socket])
		{
		case in_header:
		{
			if (on_http_header_message_received(socket, *msg))	// If done parsing
			{
				message_parsing_state[socket] = end_of_headers;
				goto begin_of_switch;
			}
			break;
		}

		case end_of_headers:

			if (message_request[socket].contentlength != 0 && message_request[socket].contentlength > msg->size() + 1)
			{
				std::cout << "Body not received in total. Waiting. " << msg->size() << "/" << message_request[socket].contentlength << std::endl;
				return;
			}

			message_request[socket].content = *msg;

			for (const auto& f : cbOnHttpRequestDone)
				f(message_request[socket]);

			break;

		case in_body:
			std::cout << ".";
			break;
		}
	}
};

class websocket_server
{
public:
	inline static const std::string WS_GUID{ "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" };
};

class endpoint
{
public:

	SOCKET s;
	sockaddr_in sockaddrin = { 0, 0, 0, 0 };

	endpoint(const SOCKET in) : s(in) {}

	SOCKET data() const
	{
		return s;
	}

	operator SOCKET() const
	{
		return s;
	}

	std::string ip()
	{
		if (sockaddrin.sin_port == 0) grab_sockaddrin_data();

		const auto& x = sockaddrin.sin_addr.S_un.S_un_b;

		std::ostringstream ss;
		ss << (int)x.s_b1 << "." << (int)x.s_b2 << "." << (int)x.s_b3 << "." << (int)x.s_b4;

		return ss.str();
	}

	uint16_t port()
	{
		if (sockaddrin.sin_port == 0) grab_sockaddrin_data();

		return sockaddrin.sin_port;
	}

private:

	void grab_sockaddrin_data()
	{
		int addrlen = sizeof(sockaddrin);
		getsockname(s, reinterpret_cast<sockaddr*>(&sockaddrin), &addrlen);
	}
};

class web_server
{
	using http_request_callback_t = bool(http_server::http_request&);

private:

	http_server http_;
	std::map<std::string_view, std::function<http_request_callback_t>> gets_, posts_, reqs_;

public:

	struct web_request
	{
		http_server::http_request http;
	};

	void start_server(const char* port)
	{
		using namespace std::placeholders;

		// Add callbacks

		http_.cbOnHttpRequestDone.emplace_back([this](auto&& ph1)
			{
				handle_http_request(std::forward<decltype(ph1)>(ph1));
			});

		http_.tcp.cb_on_connect.emplace_back([this](const SOCKET s)
			{
				http_.message_request[s].tcp = &this->http_.tcp;
				http_.message_request[s].s = s;
			});

		http_.start_server(port);
	}

	static void handle_http_request(const http_server::http_request& http_req)
	{
		endpoint ep(http_req.s);
		std::cout << "Received HTTP request from " << ep.ip() << ":" << ep.port() << std::endl;

		if (http_req.type == http_server::http_request::request_type::get) [[likely]]
		{
			std::cout << "get" << std::endl;
			tcp_server::send(http_req.s, "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello worssssssssssssssssssssssssssssssld!"); // mdlol
		}
		else if (http_req.type == http_server::http_request::request_type::post)
		{
			std::cout << "post: " << http_req.content << std::endl;
			tcp_server::send(http_req.s, "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello worssssssssssssssssssssssssssssssld!");
			// httpReq.tcp->Close(httpReq.s);
		}
	}

	void get(const std::string_view path, std::function<http_request_callback_t>&& callback)
	{
		gets_[path] = callback;
	}
};

int main()
{
	web_server ts;
	ts.start_server("81");

	while (true)
	{
		std::cin.get();
	}
}