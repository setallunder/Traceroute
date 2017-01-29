#include "InetHelper.h"

bool InetHelper::GetIP(char *host, char **address)
{
	struct addrinfo *result = NULL;

	getaddrinfo(host, "http", NULL, &result);
	for (struct addrinfo *p = result; p != NULL; p = p->ai_next)
	{
		if (p->ai_family == AF_INET) 
		{
			struct sockaddr_in  *ipv4 = (struct sockaddr_in *) p->ai_addr;
			*address = new char[16];
			inet_ntop(AF_INET, &ipv4->sin_addr, *address, 16);
			return true;
		}
	}

	return false;
}

unsigned short InetHelper::CreateChecksum(char *buffer, int length)
{
	unsigned short word;
	unsigned int sum = 0;

	//Make 16 bit words out of every two adjacent 8 bit words in the packet
	//and add them up
	for (int i = 0; i < length; i = i + 2)
	{
		word = ((buffer[i] << 8) & 0xFF00) + (buffer[i + 1] & 0xFF);
		sum = sum + (unsigned int)word;
	}

	//Take only 16 bits out of the 32 bit sum and add up the carries
	while (sum >> 16)
	{
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	return (unsigned short)sum;
}

short InetHelper::GetChecksum(char *buffer, int length)
{
	unsigned short checksum = CreateChecksum(buffer, length);

	//One's complement the result
	checksum = ~checksum;

	return checksum;
}

bool InetHelper::IsChecksumValid(char *buffer, int length)
{
	unsigned short checksum = CreateChecksum(buffer, length);

	//The sum of one's complement should be 0xFFFF
	return checksum == 0xFFFF;
}