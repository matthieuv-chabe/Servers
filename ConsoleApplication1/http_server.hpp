
#pragma once

#include <map>

#include "endpoint.hpp"
#include "tcp_server.hpp"

class http_server
{
public:


	/**
	 * \brief Represents the content of a HTTP request made by a client to the server.
	 */
	struct http_request {

		/**
		 * \brief Header request type (GET, POST, UPDATE, PUT, DELETE, ...)
		 */
		enum request_type { get, post, update, unk };

		endpoint ep{0};
		tcp_server* tcp{};

		// Header frequently used stuff
		size_t contentlength{ 0 };

		request_type type{ unk };
		std::string destination;
		std::map<std::string, std::string> headers{};
		std::string content;
	};


	/**
	 * \brief The enum representing the current parsing state of the HTTP request
	 */
	enum http_parsing_state {
		in_header,
		end_of_headers,
		in_body,
	};

	/**
	 * \brief The TCP server that powers this TCP server. Useful to add callbacks and have a low-level access to functions, like sending messages.
	 */
	tcp_server tcp;

	/**
	 * \brief List of callbacks	that gets triggered when a request is received completely
	 */
	std::vector<std::function<void(http_request& req)>> cb_on_http_request_done;

	void start_server(const char* port)
	{
		// When the TCP server receives a packet, redirect it to the HTTP function "on_tcp_msg_received"
		tcp.cb_on_recv.emplace_back([this](auto&& ph1, auto&& ph2, auto&& ph3)
			{
				on_tcp_msg_received(
					std::forward<decltype(ph1)>(ph1),
					std::forward<decltype(ph2)>(ph2),
					std::forward<decltype(ph3)>(ph3));
			});

		tcp.start_server(port);
	}

private:

	std::map<SOCKET, std::string> message_parts;

public:

	std::map<SOCKET, http_parsing_state> message_parsing_state;
	std::map<SOCKET, http_request> message_request;

private:

	void handle_http_header_line(const SOCKET& socket, const std::string&& line)
	{
		std::cout << __FUNCTION__ << line << std::endl;

		if (line.size() < 3) return;

		auto& mr = message_request[socket];

		// message_request[socket].headers.append(line).append("\r\n");
		const auto idx = line.find_first_of(':');

		// For the "GET /" line that doesn't contain ":" char
		if (idx == std::string::npos) [[unlikely]]
		{
			size_t offset = 0;
			switch (*reinterpret_cast<const uint16_t*>(&line[0]))
			{
			case 'G' + 'E' * 256: // GET - 256 is the max value for a char from "line". If line is a wstring, 
				mr.type = http_request::request_type::get;
				offset = 4; // +1 for the space between the keyword and the path
				break;
				
			case 'P' + 'O' * 256: // POST
				mr.type = http_request::request_type::post;
				offset = 5;
				break;
				
			default:	// MEH
				mr.type = http_request::request_type::unk;
				offset = line.find_first_of(' ');
			}

			const auto otherspace = line.find_first_of(' ', offset + 1);
			mr.destination = line.substr(offset, otherspace - offset);

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

			if (datasize > 2)   // Most of the time, some data will be received for the header
			{
				[[likely]]
				handle_http_header_line(socket, data.substr(0, i - 1)); // Skip the \r before \n. There'ep ALWAYS a \r before.
				data = data.substr(i + 1);
				datasize -= (i + 1);
			}

			if (datasize > 1 && data[1] == '\n')
			{
				[[unlikely]]
				data = data.substr(2); // skip \r\n
				return true;
			}

			i = 0; // Restart the loop
		}
		return false;
	}

	void on_tcp_msg_received(const SOCKET socket, const char* data, const size_t len)
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

			for (const auto& f : cb_on_http_request_done)
				f(message_request[socket]);

			break;

		case in_body:
			std::cout << ".";
			break;
		}
	}
};