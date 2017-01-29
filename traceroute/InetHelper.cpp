#include "InetHelper.h"

bool InetHelper::GetIP(char *pszRemoteHost, char **pszIPAddress)
{
	struct addrinfo *result = NULL;

	DWORD dwRetval = getaddrinfo(pszRemoteHost, "http", NULL, &result);
	for (struct addrinfo *ptr = result; ptr != NULL; ptr = ptr->ai_next)
	{
		if (ptr->ai_family == AF_INET) 
		{
			struct sockaddr_in  *sockaddr_ipv4 = (struct sockaddr_in *) ptr->ai_addr;
			*pszIPAddress = new char[16];
			inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, *pszIPAddress, 16);
			return true;
		}
	}

	return false;
}