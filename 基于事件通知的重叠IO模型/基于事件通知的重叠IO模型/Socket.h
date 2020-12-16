#pragma once
#include <WinSock2.h>

class Socket
{
protected:
	SOCKET m_hSocket;

public:
	Socket();
	virtual ~Socket();
	
	void Close();
	virtual BOOL Create(UINT nSocketPort = 0, INT nSocketType = SOCK_DGRAM, LPCSTR lpszSocketAddr = ADDR_ANY);
	int SendTo(const void* lpBuf,int nBufLen,UINT nHostPort,LPCTSTR lpszHostAddress = NULL,int nFlags = 0);
	int ReceiveFrom(void* lpBuf, int nBufLen, char* rSocketAddress, UINT& rSocketPort, int nFlags = 0);
	BOOL Listen(int nConnectionBacklog = 5);
	virtual BOOL Accept(Socket& rConnectedSocket,char* rSocketAddress, UINT& pPort);
	virtual int Send(const void* lpBuf,int nBufLen,int nFlags = 0);
	BOOL Connect(char* lpszHostAddress,UINT nHostPort);
	virtual int Receive(void* lpBuf,int nBufLen,int nFlags = 0);
};

