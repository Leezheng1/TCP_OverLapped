#define _WINSOCK_DEPRECATED_NO_WARNINGS 1

#include "OverLapppedIO.h"
#include <functional>

//包最大字节数限制为10M
#define MAX_PACKAGE_SIZE    10 * 1024 * 1024

COverLapppedIO::COverLapppedIO()
{
	m_bStop = false;
	m_nBufferCount = 0;
	m_strServer = "127.0.0.1";

	m_pBufferHead = nullptr;
	m_pBufferTail = nullptr;
	m_spServiceThread = nullptr;

	m_nPort = 4567;
}

COverLapppedIO::~COverLapppedIO()
{
}

COverLapppedIO& COverLapppedIO::GetInstance()
{
	static COverLapppedIO socketInstance;
	return socketInstance;
}

PSOCKET_OBJ COverLapppedIO::GetSocketObj(SOCKET s)
{
	PSOCKET_OBJ pSocket = (PSOCKET_OBJ)::malloc(sizeof(SOCKET_OBJ));
	if (pSocket != nullptr)
	{
		pSocket->s = s;
		pSocket->nOutstandingOps = 0;
		pSocket->lpfnAcceptEx = nullptr;
	}

	return pSocket;
}

void COverLapppedIO::FreeSocketObj(PSOCKET_OBJ pSocket)
{
	if (pSocket->s != INVALID_SOCKET)
		::closesocket(pSocket->s);

	::free(pSocket);
	pSocket = nullptr;
}

PBUFFER_OBJ COverLapppedIO::GetBufferObj(PSOCKET_OBJ pSocket, ULONG nLen)
{
	if (m_nBufferCount > WSA_MAXIMUM_WAIT_EVENTS - 1)
		return NULL;

	PBUFFER_OBJ pBuffer = (PBUFFER_OBJ)::malloc(sizeof(BUFFER_OBJ));
	if (pBuffer != nullptr)
	{
		pBuffer->buff = (char*)::malloc(nLen);
		if (pBuffer->buff != nullptr)
		{
			memset(pBuffer->buff, 0, nLen);

			pBuffer->ol.hEvent = ::WSACreateEvent();
			pBuffer->pSocket = pSocket;
			pBuffer->sAccept = INVALID_SOCKET;
			pBuffer->pNext = nullptr;

			// 将新的BUFFER_OBJ添加到列表中
			if (m_pBufferHead == nullptr)
			{
				m_pBufferHead = m_pBufferTail = pBuffer;
			} else
			{
				m_pBufferTail->pNext = pBuffer;
				m_pBufferTail = pBuffer;
			}

			m_events[++m_nBufferCount] = pBuffer->ol.hEvent;
		}
	}

	return pBuffer;
}

void COverLapppedIO::FreeBufferObj(PBUFFER_OBJ pBuffer)
{
	// 从列表中移除BUFFER_OBJ对象
	PBUFFER_OBJ pTemp = m_pBufferHead;
	BOOL bFind = FALSE;
	if (pTemp == pBuffer)
	{
		m_pBufferHead = m_pBufferTail = nullptr;//在头部
		bFind = TRUE;
	} else 
	{
		//寻找前一个节点
		while (pTemp != nullptr && pTemp->pNext != pBuffer)
			pTemp = pTemp->pNext;

		if (pTemp != nullptr)
		{
			pTemp->pNext = pBuffer->pNext;

			if (pTemp->pNext == nullptr)//是尾部
				m_pBufferTail = pTemp;

			bFind = TRUE;
		}
	}

	// 释放它占用的内存空间
	if (bFind)
	{
		m_nBufferCount--;
		::CloseHandle(pBuffer->ol.hEvent);
		::free(pBuffer->buff);
		::free(pBuffer);

		pBuffer = nullptr;
	}
}

PBUFFER_OBJ COverLapppedIO::FindBufferObj(HANDLE hEvent)
{
	PBUFFER_OBJ pBuffer = m_pBufferHead;
	while (pBuffer != nullptr)
	{
		if (pBuffer->ol.hEvent == hEvent)
			break;
		pBuffer = pBuffer->pNext;
	}

	return pBuffer;
}

void COverLapppedIO::RebuildArray()
{
	PBUFFER_OBJ pBuffer = m_pBufferHead;
	int nCount = 1;
	while (pBuffer != nullptr)
	{
		m_events[nCount++] = pBuffer->ol.hEvent;
		pBuffer = pBuffer->pNext;
	}
}

BOOL COverLapppedIO::PostAccept(PBUFFER_OBJ pBuffer)
{
	PSOCKET_OBJ pSocket = pBuffer->pSocket;
	if (pSocket->lpfnAcceptEx != NULL)
	{
		// 设置I/O类型，增加套接字上的重叠I/O计数
		pBuffer->nOperation = OP_ACCEPT;
		pSocket->nOutstandingOps++;

		// 投递此重叠I/O  
		DWORD dwBytes;

		//为新的连接创建套接字
		pBuffer->sAccept =
			::WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		BOOL bAcceptExRet = pSocket->lpfnAcceptEx(pSocket->s,	//监听套接字句柄
			pBuffer->sAccept,
			pBuffer->buff,
			BUFFER_SIZE - ((sizeof(sockaddr_in) + 16) * 2),
			sizeof(sockaddr_in) + 16,	//为本地地址预留的长度
			sizeof(sockaddr_in) + 16,	//为远程地址预留的长度
			&dwBytes,
			&pBuffer->ol);
		if (!bAcceptExRet)
		{
			if (::WSAGetLastError() != WSA_IO_PENDING)
				return FALSE;
		}
		return TRUE;
	}
	return FALSE;
};

BOOL COverLapppedIO::PostRecv(PBUFFER_OBJ pBuffer)
{
	// 设置I/O类型，增加套接字上的重叠I/O计数
	pBuffer->nOperation = OP_READ;
	pBuffer->pSocket->nOutstandingOps++;

	// 投递此重叠I/O
	DWORD dwBytes;
	DWORD dwFlags = 0;
	WSABUF buf;
	buf.buf = pBuffer->buff;
	buf.len = pBuffer->nLen;
	if (::WSARecv(pBuffer->pSocket->s, &buf, 1, &dwBytes, &dwFlags, &pBuffer->ol, NULL) != NO_ERROR)
	{
		if (::WSAGetLastError() != WSA_IO_PENDING)
			return FALSE;
	}
	return TRUE;
}

BOOL COverLapppedIO::PostSend(PBUFFER_OBJ pBuffer)
{
	// 设置I/O类型，增加套接字上的重叠I/O计数
	pBuffer->nOperation = OP_WRITE;
	pBuffer->pSocket->nOutstandingOps++;

	// 投递此重叠I/O
	DWORD dwBytes;
	DWORD dwFlags = 0;
	WSABUF buf;
	buf.buf = pBuffer->buff;
	buf.len = pBuffer->nLen;
	if (::WSASend(pBuffer->pSocket->s,
		&buf, 1, &dwBytes, dwFlags, &pBuffer->ol, NULL) != NO_ERROR)
	{
		if (::WSAGetLastError() != WSA_IO_PENDING)
			return FALSE;
	}
	return TRUE;
}

BOOL COverLapppedIO::HandleIO(PBUFFER_OBJ pBuffer)
{
	PSOCKET_OBJ pSocket = pBuffer->pSocket; // 从BUFFER_OBJ对象中提取SOCKET_OBJ对象指针，为的是方便引用
	pSocket->nOutstandingOps--;

	// 获取重叠操作结果
	DWORD dwTrans;
	DWORD dwFlags;
	BOOL bRet = ::WSAGetOverlappedResult(pSocket->s, &pBuffer->ol, &dwTrans, FALSE, &dwFlags);
	if (!bRet)
	{
		// 在此套接字上有错误发生，因此，关闭套接字，移除此缓冲区对象。
		// 如果没有其它抛出的I/O请求了，释放此缓冲区对象，否则，等待此套接字上的其它I/O也完成
		if (pSocket->s != INVALID_SOCKET)
		{
			::closesocket(pSocket->s);
			pSocket->s = INVALID_SOCKET;
		}

		if (pSocket->nOutstandingOps == 0)
			FreeSocketObj(pSocket);

		FreeBufferObj(pBuffer);
		return FALSE;
	}

	// 没有错误发生，处理已完成的I/O
	switch (pBuffer->nOperation)
	{
		case OP_ACCEPT:	// 接收到一个新的连接，并接收到了对方发来的第一个封包
		{
			// 为新客户创建一个SOCKET_OBJ对象
			PSOCKET_OBJ pClient = GetSocketObj(pBuffer->sAccept);

			// 为发送数据创建一个BUFFER_OBJ对象，这个对象会在套接字出错或者关闭时释放
			PBUFFER_OBJ pSend = GetBufferObj(pClient, BUFFER_SIZE);
			if (pSend == NULL)
			{
				printf(" Too much connections! /n");
				FreeSocketObj(pClient);
				return FALSE;
			}

			RebuildArray();
			
			// 将数据复制到发送缓冲区
			pSend->nLen = dwTrans;
			memcpy(pSend->buff, pBuffer->buff, dwTrans);

			m_strRecvBuf.append(pBuffer->buff, dwTrans);

			DecodePackages();
			//printf("Recvdata from Client :%s  \n", pSend->buff);
			
			// 投递此发送I/O（将数据回显给客户）
			if (!PostSend(pSend))
			{
				// 万一出错的话，释放上面刚申请的两个对象
				FreeSocketObj(pSocket);
				FreeBufferObj(pSend);
				return FALSE;
			}

			// 继续投递接受I/O
			PostAccept(pBuffer);
		}
		break;
		case OP_READ:	// 接收数据完成
		{
			if (dwTrans > 0)
			{
				//接受解析数据
				m_strRecvBuf.clear();
				m_strRecvBuf.append(pBuffer->buff, dwTrans);
				DecodePackages();
				
				// 创建一个缓冲区，以发送数据。这里就使用原来的缓冲区
				PBUFFER_OBJ pSend = pBuffer;
				pSend->nLen = dwTrans;

				// 投递发送I/O（将数据回显给客户）

				PostSend(pSend);

				printf("Send data to Client \n");
			} else	// 套接字关闭
			{

				// 必须先关闭套接字，以便在此套接字上投递的其它I/O也返回
				if (pSocket->s != INVALID_SOCKET)
				{
					::closesocket(pSocket->s);
					pSocket->s = INVALID_SOCKET;
				}

				if (pSocket->nOutstandingOps == 0)
					FreeSocketObj(pSocket);

				FreeBufferObj(pBuffer);
				return FALSE;
			}
		}
		break;
		case OP_WRITE:		// 发送数据完成
		{
			if (dwTrans > 0)
			{
				// 继续使用这个缓冲区投递接收数据的请求
				pBuffer->nLen = BUFFER_SIZE;
				PostRecv(pBuffer);
			} else	// 套接字关闭
			{
				// 同样，要先关闭套接字
				if (pSocket->s != INVALID_SOCKET)
				{
					::closesocket(pSocket->s);
					pSocket->s = INVALID_SOCKET;
				}

				if (pSocket->nOutstandingOps == 0)
					FreeSocketObj(pSocket);

				FreeBufferObj(pBuffer);
				return FALSE;
			}
		}
		break;
	}
	return TRUE;
}

void COverLapppedIO::Init()
{
	// 创建监听套接字，绑定到本地端口，进入监听模式
	SOCKET sListen =
		::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN si;
	si.sin_family = AF_INET;
	si.sin_port = htons(( u_short ) m_nPort);
	si.sin_addr.S_un.S_addr = inet_addr(m_strServer.c_str());

	::bind(sListen, ( sockaddr* ) &si, sizeof(si));
	::listen(sListen, 200);

	// 为监听套接字创建一个SOCKET_OBJ对象
	PSOCKET_OBJ pListen = GetSocketObj(sListen);

	// 加载扩展函数AcceptEx
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	DWORD dwBytes;
	WSAIoctl(pListen->s,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx,
		sizeof(GuidAcceptEx),
		&pListen->lpfnAcceptEx,
		sizeof(pListen->lpfnAcceptEx),
		&dwBytes,
		NULL,
		NULL);

	// 创建用来重新建立m_events数组的事件对象
	m_events[0] = ::WSACreateEvent();

	// 在此可以投递多个接受I/O请求
	for (int i = 0; i < 5; i++)
	{
		PostAccept(GetBufferObj(pListen, BUFFER_SIZE));
	}

	::WSASetEvent(m_events[0]);

	//PostRecv(GetBufferObj(pListen, BUFFER_SIZE));

	if (!m_spServiceThread)
		m_spServiceThread.reset(new std::thread(std::bind(&COverLapppedIO::ServiceProc, this)));
}

void COverLapppedIO::Uninit()
{
	m_bStop = true;

	if(m_spServiceThread)
		m_spServiceThread->detach();

	if(m_spServiceThread)
		m_spServiceThread.reset();

	if (m_spServiceThread && m_spServiceThread->joinable())
		m_spServiceThread->join();
}

void COverLapppedIO::ServiceProc(void)
{
	while (!m_bStop)
	{
		int nIndex =
			::WSAWaitForMultipleEvents(m_nBufferCount + 1, m_events, FALSE, WSA_INFINITE, FALSE);
		if (nIndex == WSA_WAIT_FAILED)
		{
			printf("WSAWaitForMultipleEvents() failed /n");
			break;
		}
		nIndex = nIndex - WSA_WAIT_EVENT_0;
		for (int i = 0; i <= nIndex; i++)
		{
			int nRet = ::WSAWaitForMultipleEvents(1, &m_events[i], TRUE, 0, FALSE);
			if (nRet == WSA_WAIT_TIMEOUT)
				continue;
			else
			{
				::WSAResetEvent(m_events[i]);
				// 重新建立m_events数组
				if (i == 0)
				{
					RebuildArray();
					continue;
				}

				// 处理这个I/O
				PBUFFER_OBJ pBuffer = FindBufferObj(m_events[i]);
				if (pBuffer != NULL)
				{
					if (!HandleIO(pBuffer))
						RebuildArray();
				}
			}
		}
	}
}

bool COverLapppedIO::DecodePackages()
{
	//一定要放在一个循环里面解包，因为可能一片数据中有多个包，
	//对于数据收不全，
	while (true)
	{
		//接收缓冲区不够一个包头大小
		if (m_strRecvBuf.length() <= sizeof(DataHeader))
			break;

		DataHeader header;
		memcpy_s(&header, sizeof(DataHeader), m_strRecvBuf.data(), sizeof(DataHeader));

		//防止包头定义的数据是一些错乱的数据，这里最大限制每个包大小为10M
		if (header.dataLength >= MAX_PACKAGE_SIZE || header.dataLength <= 0)
		{
			OutputDebugString(L"Recv a illegal package, originsize=%d.");
			m_strRecvBuf.clear();
			return false;
		}

		//接收缓冲区不够一个整包大小（包头+包体）
		if (m_strRecvBuf.length() < sizeof(DataHeader) + header.dataLength)
			break;

		//去除包头信息
		m_strRecvBuf.erase(0, sizeof(DataHeader));
		//拿到包体
		std::string strBody;
		strBody.append(m_strRecvBuf.c_str(), header.dataLength);

		//去除包体信息
		m_strRecvBuf.erase(0, header.dataLength);

		printf("\n----------------------------------------- \n");
		printf("RecvData from Client\n");
		printf("header commpress: %d \n", header.commpress );
		printf("header dataLength: %d\n",header.dataLength);
		printf("header appType: %d \n",header.appType);
		printf("header cmd: %d \n", header.cmd);

		printf("content: [%s] \n", strBody.c_str());

		printf("\n----------------------------------------- \n");

		//TODO
		//回调通知

	}// end while

	return true;
	
}

void COverLapppedIO::SendData(const std::string& strBuffer, AppType nAppType, Cmd nCmd)
{
	PBUFFER_OBJ pBuffer = m_pBufferHead->pNext;
	
	//找到除了监听socket外的其他socket
	while (pBuffer)
	{
		if (pBuffer->pSocket->lpfnAcceptEx == nullptr)
		{
			m_strSendBuf.clear();
			int32_t datalength = ( int32_t ) strBuffer.length();

			//插入包头
			DataHeader header = {};
			memset(&header, 0, sizeof(header));

			header.commpress = 0;
			header.dataLength = datalength;
			header.appType = nAppType;
			header.cmd = nCmd;

			m_strSendBuf.append(( const char* ) &header, sizeof(header));
			m_strSendBuf.append(strBuffer);

			pBuffer->nLen = m_strSendBuf.size();
			memcpy_s(pBuffer->buff, 1024, m_strSendBuf.data(), m_strSendBuf.size());

			PostSend(pBuffer);
		}

		pBuffer = pBuffer->pNext;
	}
	
}