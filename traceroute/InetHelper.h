#pragma once

#include <winsock2.h>
#include <Ws2tcpip.h>

class InetHelper
{
public:
	static bool GetIP(char *pszRemoteHost, char **pszIPAddress);
	static short GetChecksum(char *pBuffer, int nLen);
	static bool IsChecksumValid(char *pBuffer, int nLen);
private:
	InetHelper(){}
	static unsigned short CreateChecksum(char *pBuffer, int nLen);
};
