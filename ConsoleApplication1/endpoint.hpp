#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include <string>
#include <sstream>

class endpoint
{
public:

	SOCKET s;
	sockaddr_in sockaddrin = { 0, 0, 0, 0 };

	endpoint(const SOCKET in) : s(in) {}

	[[nodiscard]]
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
