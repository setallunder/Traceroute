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

int main(int argc, char* argv[])
{
	if (argc < 2 || argc > 6)
	{
		return 0;
	}

	if (Initialize() == false)
	{
		return -1;
	}

	int nSequence = 0;
	int nTimeOut = 3000;	//Request time out for echo request (in milliseconds)
	int nHopCount = 30;	//Number of hops to retry
	int nMaxRetries = 3;

	char *pszRemoteIP = NULL, *pSendBuffer = NULL, *pszRemoteHost = NULL;

	pszRemoteHost = argv[1];

	for (int i = 2; i < argc; ++i)
	{
		switch (i)
		{
		case 2:
			nHopCount = atoi(argv[2]);
			break;
		case 3:
			break;
		case 4:
			nMaxRetries = atoi(argv[4]);
			break;
		case 5:
			nTimeOut = atoi(argv[5]);
			break;
		}
	}
	if (InetHelper::GetIP(pszRemoteHost, &pszRemoteIP) == false)
	{
		cerr << endl << "Unable to resolve hostname" << endl;
		return -1;
	}

	cout << "Tracing route to " << pszRemoteHost << " [" << pszRemoteIP << "] over a maximum of " << nHopCount
		<< " hops." << endl << endl;
	ICMPheader sendHdr;

	SOCKET sock;
	sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);	//Create a raw socket which will use ICMP

	SOCKADDR_IN destAddr;	//Dest address to send the ICMP request
	destAddr.sin_addr.S_un.S_addr = inet_addr(pszRemoteIP);
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = rand();	//Pick a random port

	SOCKADDR_IN remoteAddr;
	int nRemoteAddrLen = sizeof(remoteAddr);

	int nResult = 0;
	SYSTEMTIME timeSend, timeRecv;

	fd_set fdRead;

	timeval timeInterval = { 0, 0 };
	timeInterval.tv_usec = nTimeOut * 1000;

	sendHdr.nId = htons(rand());	//Set the transaction Id
	sendHdr.byCode = 0;	//Zero for ICMP echo and reply messages
	sendHdr.byType = 8;	//Eight for ICMP echo message

	int nHopsTraversed = 0;
	int nTTL = 1;	//The all important TTL value, which will be incremented with each hop

	while (nHopsTraversed < nHopCount &&
		memcmp(&destAddr.sin_addr, &remoteAddr.sin_addr, sizeof(in_addr)) != 0)
	{
		cout << "  " << nHopsTraversed + 1;

		//Set the TTL value for the message
		if (setsockopt(sock, IPPROTO_IP, IP_TTL, (char *)&nTTL, sizeof(nTTL)) == SOCKET_ERROR)
		{
			cerr << endl << "An error occured in setsockopt operation: " << "WSAGetLastError () = " << WSAGetLastError() << endl;
			UnInitialize();
			delete[]pSendBuffer;
			return -1;
		}

		//Create the message buffer, which is big enough to store the header and the message data
		pSendBuffer = new char[sizeof(ICMPheader)];
		sendHdr.nSequence = htons(nSequence++);
		sendHdr.nChecksum = 0;	//Checksum is calculated later on

		memcpy_s(pSendBuffer, sizeof(ICMPheader), &sendHdr, sizeof(ICMPheader));	//Copy the message header in the buffer

																		//Calculate checksum over ICMP header and message data
		sendHdr.nChecksum = htons(InetHelper::GetChecksum(pSendBuffer, sizeof(ICMPheader)));

		//Copy the message header back into the buffer
		memcpy_s(pSendBuffer, sizeof(ICMPheader), &sendHdr, sizeof(ICMPheader));

		//The number of time the request was sent out
		int nRetries = 0;

		IPheader ipHdr;

		//This flag indicates if we got a response to any request it
		//basically helps in determining whether all requests timed out or not, and
		//if they did then we print an appropriate message
		bool bGotAResponse = false;
		while (nRetries < nMaxRetries)
		{
			nResult = sendto(sock, pSendBuffer, sizeof(ICMPheader), 0, (SOCKADDR *)&destAddr,
				sizeof(SOCKADDR_IN));

			//Save the time at which the ICMP echo message was sent
			::GetSystemTime(&timeSend);

			if (nResult == SOCKET_ERROR)
			{
				cerr << endl << "An error occured in sendto operation: " << "WSAGetLastError () = " << WSAGetLastError() << endl;
				UnInitialize();
				delete[]pSendBuffer;
				return -1;
			}

			FD_ZERO(&fdRead);
			FD_SET(sock, &fdRead);

			if ((nResult = select(0, &fdRead, NULL, NULL, &timeInterval))
				== SOCKET_ERROR)
			{
				cerr << endl << "An error occured in select operation: " << "WSAGetLastError () = " <<
					WSAGetLastError() << endl;
				delete[]pSendBuffer;
				return -1;
			}

			if (nResult > 0 && FD_ISSET(sock, &fdRead))
			{
				//Allocate a large buffer to store the response
				char *pRecvBuffer = new char[1500];

				//We got a response
				if ((nResult = recvfrom(sock, pRecvBuffer, 1500, 0, (SOCKADDR *)&remoteAddr, &nRemoteAddrLen))
					== SOCKET_ERROR)
				{
					cerr << endl << "An error occured in recvfrom operation: " << "WSAGetLastError () = " <<
						WSAGetLastError() << endl;
					UnInitialize();
					delete[]pSendBuffer;
					delete[]pRecvBuffer;
					return -1;
				}

				//Get the time at which response is received
				::GetSystemTime(&timeRecv);

				bGotAResponse = true;

				//We got a response so we construct the ICMP header and message out of it
				ICMPheader recvHdr;
				char *pICMPbuffer = NULL;

				//The response includes the IP header as well, so we move 20 bytes ahead to read the ICMP header
				pICMPbuffer = pRecvBuffer + sizeof(IPheader);

				//ICMP message length is calculated by subtracting the IP header size from the 
				//total bytes received
				int nICMPMsgLen = nResult - sizeof(IPheader);

				//Construct the ICMP header
				memcpy_s(&recvHdr, sizeof(recvHdr), pICMPbuffer, sizeof(recvHdr));

				//Construct the IP header from the response
				memcpy_s(&ipHdr, sizeof(ipHdr), pRecvBuffer, sizeof(ipHdr));

				recvHdr.nId = recvHdr.nId;
				recvHdr.nSequence = recvHdr.nSequence;
				recvHdr.nChecksum = ntohs(recvHdr.nChecksum);

				//Check if the checksum is correct
				if (InetHelper::IsChecksumValid(pICMPbuffer, nICMPMsgLen))
				{
					//All's OK
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
					//There was an error in the response
					cout << "\t!";
				}

				delete[]pRecvBuffer;
			}
			else
			{
				//Request timed out so we try again
				cout << "\t*";
			}
			++nRetries;
		}

		if (bGotAResponse == false)
		{
			//Since we didn't get any ICMP time exceeded message from this server so 
			//we try with the next one, with the TTL value being incremented
			cout << "\tRequest timed out.";
		}
		else
		{
			//Print the hop's IP and resolved name
			in_addr in;
			in.S_un.S_addr = ipHdr.nSrcAddr;

			char *pszSrcAddr = inet_ntoa(in);
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
		cerr << endl << "An error occured in WSAStartup operation: " << "WSAGetLastError () = " << WSAGetLastError() << endl;
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
		cerr << endl << "An error occured in WSACleanup operation: WSAGetLastError () = " << WSAGetLastError() << endl;
		return false;
	}

	return true;
}