#pragma once

#include "http_server.hpp"

class web_server
{
	struct web_response;

	struct web_request
	{
		http_server::http_request http;
	};

	struct web_response
	{
		// Links
		web_request* wr;

		// Inner informations
		std::stringstream ss;

		uint16_t status_code = 200;
		std::unordered_map<uint16_t, const std::string> status_text { { 200, "OK" }, { 404, "Not Found" }, { 201, "Created"} };

		std::map<std::string, std::string> headers;

		std::string make_request() const
		{
			std::stringstream sso;

			sso << "HTTP/1.1 " << status_code << " " << status_text.at(status_code) << "\r\n"
				<< "Content-Type: text/html\r\n"
				<< "Content-Length: " << ss.str().size() << "\r\n"
				<< "Connection: close\r\n\r\n"
				<< ss.str();
			
			return sso.str();
		}

		void send() const
		{
			std::cout << __FUNCTION__ " " << wr->http << std::endl;
			tcp_server::send(wr->http.ep, make_request());
		}
	};

	using web_callback_request = web_server::web_request;
	using web_callback_response = web_server::web_response;
	using web_callback_t = std::function<bool(web_callback_request&, web_callback_response&)>;

private:

	http_server http_;
	std::map<std::string_view, web_callback_t> gets_, posts_, reqs_;

public:

	void start_server(uint16_t port)
	{
		using namespace std::placeholders;

		// Add callbacks

		http_.cb_on_http_request_done.emplace_back([this](auto&& ph1)
			{
				handle_http_request(std::forward<decltype(ph1)>(ph1));
			});

		http_.tcp.cb_on_connect.emplace_back([this](const SOCKET s)
			{
				http_.message_request[s].tcp = &this->http_.tcp;
				http_.message_request[s].ep = s;
			});

		http_.start_server(std::to_string(port).c_str());
	}

	void handle_http_request(const http_server::http_request& http_req)
	{
		endpoint ep(http_req.ep);
		std::cout << "Received HTTP request from " << ep.ip() << ":" << ep.port() << std::endl;

		switch (http_req.type)
		{

			// CORS shit
			case http_server::http_request::request_type::options:
			{
				std::cout << "OPTIONS" << std::endl;
				tcp_server::send(http_req.ep, "HTTP/1.1\r\nAllow: OPTIONS, GET, HEAD, POST, PUT\r\n\r\n");
				break;
			}

			case http_server::http_request::request_type::get:
			{
				// std::cout << "GET request" << std::endl;
				auto it = gets_.find(http_req.destination);
				if (it != gets_.end())
				{
					web_request wr;
					wr.http = http_req;

					web_response res;
					res.wr = &wr;
					it->second(wr, res);
				}
				else
				{
					std::cout << "No GET callback for " << http_req.destination << std::endl;
				}
				break;
			}
			
			case http_server::http_request::request_type::post:
			{										 
				// std::cout << "POST request" << std::endl;
				auto it = posts_.find(http_req.destination);
				if (it != posts_.end())
				{
					web_request wr;
					wr.http = http_req;

					web_response res;
					res.wr = &wr;

					it->second(wr, res);
				}
				else
				{
					std::cout << "No POST callback for " << http_req.destination << std::endl;
				}
				break;
			}
		}

		// tcp_server::send(http_req.ep, "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello worssssssssssssssssssssssssssssssld!"); // mdlol
	}

	void get(const std::string_view path, web_callback_t&& callback)
	{
		gets_[path] = callback;
	}

	void post(const std::string_view path, web_callback_t&& callback)
	{
		posts_[path] = callback;
	}
};
