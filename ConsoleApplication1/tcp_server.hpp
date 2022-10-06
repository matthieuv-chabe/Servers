
#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <iostream>
#include <thread>
#include <vector>
#include <functional>

#pragma comment (lib, "Ws2_32.lib")

constexpr auto DEFAULT_BUFLEN = 1000;

class tcp_server
{
private:

	WSADATA wsa_data_;

	SOCKET listen_socket_ = INVALID_SOCKET;			// The socket used to accept connections.

	std::vector<SOCKET> sockets_{};					// List of all sockets
	std::thread accept_thread_;						// The thread that accepts new connections
	std::vector<std::thread> listener_thread_{};	// One thread per connection

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

		addrinfo hints{};
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

		accept_thread_ = std::thread([this] { thread_accept(); });
		accept_thread_.detach();
	}

private:

	[[noreturn]]
	void thread_accept()
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

			std::thread listener_thread([this, client_socket] { thread_listener(client_socket); });
			listener_thread.detach();
		}
	}

	void thread_listener(const SOCKET client_socket)
	{

		while (true)
		{
			char recvbuf[DEFAULT_BUFLEN];
			constexpr size_t recvbuflen = DEFAULT_BUFLEN;

			if (const auto i_result = recv(client_socket, recvbuf, recvbuflen, 0); i_result > 0) [[likely]]
			{
				// Some data received

				for (const auto& f : cb_on_recv)
					f(client_socket, recvbuf, i_result);
			}
			else if (i_result == 0)
			{
				// Disconnect asked from the other parties

				for (const auto& f : cb_on_disconnect)
					f(client_socket);

				std::cout << "Disconnected !" << std::endl;

				this->sockets_.erase(std::remove(sockets_.begin(), sockets_.end(), client_socket), sockets_.end());
				break;
			}
			else
			{


				for (const auto f : cb_on_disconnect)
					f(client_socket);

				std::cout << "Error !" << std::endl;

				this->sockets_.erase(std::remove(sockets_.begin(), sockets_.end(), client_socket), sockets_.end());
				break;
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

	static void send(const SOCKET s, const std::string&& message)
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

	static void close(const SOCKET s)
	{
		std::cout << "closing" << std::endl;
		shutdown(s, SD_BOTH);
	}
};
