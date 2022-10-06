// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "web_server.hpp"

int main()
{
	web_server ws;

	ws.get("/", [](auto&& req, auto&& res)
		{
			res.ss << "Hello world !";
			res.send();
			return true;
		});
	
	ws.start_server(81);

	while (true)
	{
		std::cin.get();
	}
}