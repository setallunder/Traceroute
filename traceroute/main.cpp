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
void FillHeader(ICMPheader sendHdr, char **pSendBuffer, int nSequence); 
int SendICMP(SOCKET sock,
	char **pSendBuffer,
	SOCKADDR_IN *destAddr,
	int nTimeOut,
	SOCKADDR_IN *remoteAddr,
	char **pRecvBuffer,
	int *received);

const int Retries = 3;

int main(int argc, char* argv[])
{
	if (argc % 2 != 0)
	{
		return 0;
	}

	if (Initialize() == false)
	{
		return -1;
	}

	int nSequence = 0;
	int nTimeOut = 3000;
	int nHopCount = 30;

	char *pszRemoteIP = NULL, *pszRemoteHost = NULL;

	pszRemoteHost = argv[argc - 1];

	for (int i = 1; i < argc - 1; i += 2)
	{
		if (strcmp(argv[i], "-h") == 0) 
		{
			nHopCount = atoi(argv[i + 1]);
		}
		else if (strcmp(argv[i], "-w") == 0)
		{
			nTimeOut = atoi(argv[i + 1]);
		}
	}

	if (InetHelper::GetIP(pszRemoteHost, &pszRemoteIP) == false)
	{
		cerr << endl << "Unable to resolve hostname" << endl;
		return -1;
	}

	cout << "Tracing route to " << pszRemoteHost << " [" << pszRemoteIP << "] over a maximum of " << nHopCount
		<< " hops." << endl << endl;
	int nTTL = 1;

	SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

	SOCKADDR_IN destAddr;
	destAddr.sin_addr.S_un.S_addr = inet_addr(pszRemoteIP);
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = rand();

	SOCKADDR_IN remoteAddr;

	ICMPheader sendHdr;
	sendHdr.nId = htons(rand());
	sendHdr.byCode = 0;	//ICMP echo and reply messages
	sendHdr.byType = 8;	//ICMP echo message

	int nHopsTraversed = 0;

	while (nHopsTraversed < nHopCount &&
		memcmp(&destAddr.sin_addr, &remoteAddr.sin_addr, sizeof(in_addr)) != 0)
	{
		cout << nHopsTraversed + 1;

		char *pSendBuffer = new char[sizeof(ICMPheader)];

		if (setsockopt(sock, IPPROTO_IP, IP_TTL, (char *)&nTTL, sizeof(nTTL)) == SOCKET_ERROR)
		{
			UnInitialize();
			delete[]pSendBuffer;
			return -1;
		}

		FillHeader(sendHdr, &pSendBuffer, nSequence++);

		bool bGotAResponse = false;
		int nRetries = 0;

		while (nRetries < Retries)
		{
			SYSTEMTIME timeSend, timeRecv;
			::GetSystemTime(&timeSend);

			char *pRecvBuffer = new char[1500];

			int received;

			int sendResult = SendICMP(sock, &pSendBuffer, &destAddr, nTimeOut, &remoteAddr, &pRecvBuffer, &received);
			if (sendResult == -1)
			{
				UnInitialize();
				delete[]pSendBuffer;
				delete[]pRecvBuffer;
				return -1;
			}
			else if (sendResult == 0)
			{
				cout << "\t*";
			}
			else
			{
				bGotAResponse = true;

				::GetSystemTime(&timeRecv);

				char *pICMPbuffer = pRecvBuffer + sizeof(IPheader);
				int nICMPMsgLen = received - sizeof(IPheader);

				if (InetHelper::IsChecksumValid(pICMPbuffer, nICMPMsgLen))
				{
					int nSec = timeRecv.wSecond - timeSend.wSecond;
					if (nSec < 0)
					{
						nSec = nSec + 60;
					}

					int nMilliSec = abs(timeRecv.wMilliseconds - timeSend.wMilliseconds);

					int nRoundTripTime = 0;
					nRoundTripTime = abs(nSec * 1000 - nMilliSec);

					cout << '\t' << nRoundTripTime << " ms";
				}
				else
				{
					cout << "\t!";
				}
			}

			delete[]pRecvBuffer;

			++nRetries;
		}

		if (bGotAResponse == false)
		{
			cout << "\tRequest timed out.";
		}
		else
		{
			char *pszSrcAddr = inet_ntoa(remoteAddr.sin_addr);
			char szHostName[NI_MAXHOST];

			if (getnameinfo((SOCKADDR*)&remoteAddr,
				sizeof(SOCKADDR_IN),
				szHostName,
				NI_MAXHOST,
				NULL,
				0,
				NI_NUMERICSERV) == SOCKET_ERROR)
			{
				strncpy_s(szHostName, NI_MAXHOST, "Error resolving host name", _TRUNCATE);
			}
			cout << '\t' << szHostName << " [" << pszSrcAddr << "]";
		}

		cout << endl << '\r';
		++nHopsTraversed;
		++nTTL;

		delete[]pSendBuffer;
	}

	if (UnInitialize() == false)
	{
		return -1;
	}

	cout << endl << "Trace complete." << endl;

	return 0;
}

bool Initialize()
{
	//Initialize WinSock
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) == SOCKET_ERROR)
	{
		return false;
	}

	SYSTEMTIME time;
	::GetSystemTime(&time);

	//Seed the random number generator with current millisecond value
	srand(time.wMilliseconds);

	return true;
}

bool UnInitialize()
{
	//Cleanup
	if (WSACleanup() == SOCKET_ERROR)
	{
		return false;
	}

	return true;
}

void FillHeader(ICMPheader sendHdr, char **pSendBuffer, int nSequence)
{
	sendHdr.nSequence = htons(nSequence++);
	sendHdr.nChecksum = 0;

	memcpy_s(*pSendBuffer, sizeof(ICMPheader), &sendHdr, sizeof(ICMPheader));

	sendHdr.nChecksum = htons(InetHelper::GetChecksum(*pSendBuffer, sizeof(ICMPheader)));

	memcpy_s(*pSendBuffer, sizeof(ICMPheader), &sendHdr, sizeof(ICMPheader));
}

int SendICMP(SOCKET sock,
	char **pSendBuffer, 
	SOCKADDR_IN *destAddr,
	int nTimeOut,
	SOCKADDR_IN *remoteAddr,
	char **pRecvBuffer,
	int *received)
{
	int nResult = sendto(sock, *pSendBuffer, sizeof(ICMPheader), 0, (SOCKADDR *)destAddr,
		sizeof(SOCKADDR_IN));

	if (nResult == SOCKET_ERROR)
	{
		return -1;
	}

	fd_set fdRead;

	FD_ZERO(&fdRead);
	FD_SET(sock, &fdRead);

	timeval timeInterval = { 0, 0 };
	timeInterval.tv_usec = nTimeOut * 1000;

	if ((nResult = select(0, &fdRead, NULL, NULL, &timeInterval))
		== SOCKET_ERROR)
	{
		return -1;
	}

	if (nResult > 0 && FD_ISSET(sock, &fdRead))
	{
		int nRemoteAddrLen = sizeof(SOCKADDR_IN);
		if ((nResult = recvfrom(sock, *pRecvBuffer, 1500, 0, (SOCKADDR *)remoteAddr, &nRemoteAddrLen))
			== SOCKET_ERROR)
		{
			return -1;
		}
		else 
		{
			*received = nResult;
			return 1;
		}
	}
	else
	{
		return 0;
	}
}