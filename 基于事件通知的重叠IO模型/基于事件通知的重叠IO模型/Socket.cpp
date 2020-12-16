#define _WINSOCK_DEPRECATED_NO_WARNINGS 
#define _CRT_SECURE_NO_WARNINGS

#include "Socket.h"
#pragma comment(lib,"ws2_32.lib")

Socket::Socket()
{
	WSAData wsa = { 0 };
	WSAStartup(MAKEWORD(2, 2), &wsa);
}

Socket::~Socket()
{
	Close();
}

void Socket::Close()
{
	if (m_hSocket != INVALID_SOCKET)
		closesocket(m_hSocket);
	WSACleanup();
}

BOOL Socket::Create(UINT nSocketPort /*= 0*/, INT nSocketType /*= SOCK_DGRAM*/, LPCSTR lpszSocketAddr /*= ADDR_ANY*/)
{
	m_hSocket = socket(AF_INET, nSocketType, 0);
	if (INVALID_SOCKET == m_hSocket)
		return FALSE;
	SOCKADDR_IN addr = { AF_INET,htons(nSocketPort) };
	if (lpszSocketAddr)
		addr.sin_addr.S_un.S_addr = inet_addr(lpszSocketAddr);
	return !bind(m_hSocket, (sockaddr*)&addr, sizeof(addr));
}

int Socket::SendTo(const void* lpBuf, int nBufLen, UINT nHostPort, LPCTSTR lpszHostAddress /*= NULL*/, int nFlags /*= 0*/)
{
	SOCKADDR_IN addr = { AF_INET,htons(nHostPort) };
	if(lpszHostAddress)
		addr.sin_addr.S_un.S_addr = inet_addr((char*)lpszHostAddress);
	return sendto(m_hSocket, (char*)lpBuf, nBufLen, nFlags, (sockaddr*)&addr, sizeof(SOCKADDR_IN));
}

int Socket::ReceiveFrom(void* lpBuf, int nBufLen, char* rSocketAddress, UINT& rSocketPort, int nFlags /*= 0*/)
{
	SOCKADDR_IN from = { AF_INET };
	int len = sizeof(from);
	int nRet = recvfrom(m_hSocket, (char*)lpBuf, nBufLen, nFlags, (sockaddr*)&from, &len);
	if (nRet > 0)
	{
		strcpy(rSocketAddress, inet_ntoa(from.sin_addr));
		rSocketPort = htons(from.sin_port);
	}
	return nRet;
}

BOOL Socket::Listen(int nConnectionBacklog /*= 5*/)
{
	return !listen(m_hSocket, nConnectionBacklog);
}

BOOL Socket::Accept(Socket& rConnectedSocket, char* rSocketAddress, UINT& pPort)
{
	SOCKADDR_IN addr = { AF_INET };
	int len = sizeof(addr);
	SOCKET sock = accept(m_hSocket, (sockaddr*)&addr, &len);
	if (INVALID_SOCKET == sock)
		return FALSE;
	rConnectedSocket.m_hSocket = sock;
	strcpy(rSocketAddress, inet_ntoa(addr.sin_addr));
	pPort = htons(addr.sin_port);
	return TRUE;
}

int Socket::Send(const void* lpBuf, int nBufLen, int nFlags /*= 0*/)
{
	 return send(m_hSocket, (char*)lpBuf, nBufLen, nFlags);
}

BOOL Socket::Connect(char* lpszHostAddress, UINT nHostPort)
{
	SOCKADDR_IN addr = { AF_INET,htons(nHostPort) };
	addr.sin_addr.S_un.S_addr = inet_addr(lpszHostAddress);
	int len = sizeof(addr);
	return !connect(m_hSocket, (sockaddr*)&addr, len);
}

int Socket::Receive(void* lpBuf, int nBufLen, int nFlags /*= 0*/)
{
	 return recv(m_hSocket, (char*)lpBuf, nBufLen, nFlags);
}

