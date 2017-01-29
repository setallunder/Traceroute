#pragma once

#include <winsock2.h>
#include <Ws2tcpip.h>

class InetHelper
{
public:
	static bool GetIP(char *pszRemoteHost, char **pszIPAddress);
private:
	InetHelper(){}
};
