#define _WINSOCK_DEPRECATED_NO_WARNINGS 1

#include "OverLapppedIO.h"
#include <functional>

//������ֽ�������Ϊ10M
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

			// ���µ�BUFFER_OBJ��ӵ��б���
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
	// ���б����Ƴ�BUFFER_OBJ����
	PBUFFER_OBJ pTemp = m_pBufferHead;
	BOOL bFind = FALSE;
	if (pTemp == pBuffer)
	{
		m_pBufferHead = m_pBufferTail = nullptr;//��ͷ��
		bFind = TRUE;
	} else 
	{
		//Ѱ��ǰһ���ڵ�
		while (pTemp != nullptr && pTemp->pNext != pBuffer)
			pTemp = pTemp->pNext;

		if (pTemp != nullptr)
		{
			pTemp->pNext = pBuffer->pNext;

			if (pTemp->pNext == nullptr)//��β��
				m_pBufferTail = pTemp;

			bFind = TRUE;
		}
	}

	// �ͷ���ռ�õ��ڴ�ռ�
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
		// ����I/O���ͣ������׽����ϵ��ص�I/O����
		pBuffer->nOperation = OP_ACCEPT;
		pSocket->nOutstandingOps++;

		// Ͷ�ݴ��ص�I/O  
		DWORD dwBytes;

		//Ϊ�µ����Ӵ����׽���
		pBuffer->sAccept =
			::WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		BOOL bAcceptExRet = pSocket->lpfnAcceptEx(pSocket->s,	//�����׽��־��
			pBuffer->sAccept,
			pBuffer->buff,
			BUFFER_SIZE - ((sizeof(sockaddr_in) + 16) * 2),
			sizeof(sockaddr_in) + 16,	//Ϊ���ص�ַԤ���ĳ���
			sizeof(sockaddr_in) + 16,	//ΪԶ�̵�ַԤ���ĳ���
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
	// ����I/O���ͣ������׽����ϵ��ص�I/O����
	pBuffer->nOperation = OP_READ;
	pBuffer->pSocket->nOutstandingOps++;

	// Ͷ�ݴ��ص�I/O
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
	// ����I/O���ͣ������׽����ϵ��ص�I/O����
	pBuffer->nOperation = OP_WRITE;
	pBuffer->pSocket->nOutstandingOps++;

	// Ͷ�ݴ��ص�I/O
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
	PSOCKET_OBJ pSocket = pBuffer->pSocket; // ��BUFFER_OBJ��������ȡSOCKET_OBJ����ָ�룬Ϊ���Ƿ�������
	pSocket->nOutstandingOps--;

	// ��ȡ�ص��������
	DWORD dwTrans;
	DWORD dwFlags;
	BOOL bRet = ::WSAGetOverlappedResult(pSocket->s, &pBuffer->ol, &dwTrans, FALSE, &dwFlags);
	if (!bRet)
	{
		// �ڴ��׽������д���������ˣ��ر��׽��֣��Ƴ��˻���������
		// ���û�������׳���I/O�����ˣ��ͷŴ˻��������󣬷��򣬵ȴ����׽����ϵ�����I/OҲ���
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

	// û�д���������������ɵ�I/O
	switch (pBuffer->nOperation)
	{
		case OP_ACCEPT:	// ���յ�һ���µ����ӣ������յ��˶Է������ĵ�һ�����
		{
			// Ϊ�¿ͻ�����һ��SOCKET_OBJ����
			PSOCKET_OBJ pClient = GetSocketObj(pBuffer->sAccept);

			// Ϊ�������ݴ���һ��BUFFER_OBJ���������������׽��ֳ�����߹ر�ʱ�ͷ�
			PBUFFER_OBJ pSend = GetBufferObj(pClient, BUFFER_SIZE);
			if (pSend == NULL)
			{
				printf(" Too much connections! /n");
				FreeSocketObj(pClient);
				return FALSE;
			}

			RebuildArray();
			
			// �����ݸ��Ƶ����ͻ�����
			pSend->nLen = dwTrans;
			memcpy(pSend->buff, pBuffer->buff, dwTrans);

			m_strRecvBuf.append(pBuffer->buff, dwTrans);

			DecodePackages();
			//printf("Recvdata from Client :%s  \n", pSend->buff);
			
			// Ͷ�ݴ˷���I/O�������ݻ��Ը��ͻ���
			if (!PostSend(pSend))
			{
				// ��һ����Ļ����ͷ�������������������
				FreeSocketObj(pSocket);
				FreeBufferObj(pSend);
				return FALSE;
			}

			// ����Ͷ�ݽ���I/O
			PostAccept(pBuffer);
		}
		break;
		case OP_READ:	// �����������
		{
			if (dwTrans > 0)
			{
				//���ܽ�������
				m_strRecvBuf.clear();
				m_strRecvBuf.append(pBuffer->buff, dwTrans);
				DecodePackages();
				
				// ����һ�����������Է������ݡ������ʹ��ԭ���Ļ�����
				PBUFFER_OBJ pSend = pBuffer;
				pSend->nLen = dwTrans;

				// Ͷ�ݷ���I/O�������ݻ��Ը��ͻ���

				PostSend(pSend);

				printf("Send data to Client \n");
			} else	// �׽��ֹر�
			{

				// �����ȹر��׽��֣��Ա��ڴ��׽�����Ͷ�ݵ�����I/OҲ����
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
		case OP_WRITE:		// �����������
		{
			if (dwTrans > 0)
			{
				// ����ʹ�����������Ͷ�ݽ������ݵ�����
				pBuffer->nLen = BUFFER_SIZE;
				PostRecv(pBuffer);
			} else	// �׽��ֹر�
			{
				// ͬ����Ҫ�ȹر��׽���
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
	// ���������׽��֣��󶨵����ض˿ڣ��������ģʽ
	SOCKET sListen =
		::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN si;
	si.sin_family = AF_INET;
	si.sin_port = htons(( u_short ) m_nPort);
	si.sin_addr.S_un.S_addr = inet_addr(m_strServer.c_str());

	::bind(sListen, ( sockaddr* ) &si, sizeof(si));
	::listen(sListen, 200);

	// Ϊ�����׽��ִ���һ��SOCKET_OBJ����
	PSOCKET_OBJ pListen = GetSocketObj(sListen);

	// ������չ����AcceptEx
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

	// �����������½���m_events������¼�����
	m_events[0] = ::WSACreateEvent();

	// �ڴ˿���Ͷ�ݶ������I/O����
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
				// ���½���m_events����
				if (i == 0)
				{
					RebuildArray();
					continue;
				}

				// �������I/O
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
	//һ��Ҫ����һ��ѭ������������Ϊ����һƬ�������ж������
	//���������ղ�ȫ��
	while (true)
	{
		//���ջ���������һ����ͷ��С
		if (m_strRecvBuf.length() <= sizeof(DataHeader))
			break;

		DataHeader header;
		memcpy_s(&header, sizeof(DataHeader), m_strRecvBuf.data(), sizeof(DataHeader));

		//��ֹ��ͷ�����������һЩ���ҵ����ݣ������������ÿ������СΪ10M
		if (header.dataLength >= MAX_PACKAGE_SIZE || header.dataLength <= 0)
		{
			OutputDebugString(L"Recv a illegal package, originsize=%d.");
			m_strRecvBuf.clear();
			return false;
		}

		//���ջ���������һ��������С����ͷ+���壩
		if (m_strRecvBuf.length() < sizeof(DataHeader) + header.dataLength)
			break;

		//ȥ����ͷ��Ϣ
		m_strRecvBuf.erase(0, sizeof(DataHeader));
		//�õ�����
		std::string strBody;
		strBody.append(m_strRecvBuf.c_str(), header.dataLength);

		//ȥ��������Ϣ
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
		//�ص�֪ͨ

	}// end while

	return true;
	
}

void COverLapppedIO::SendData(const std::string& strBuffer, AppType nAppType, Cmd nCmd)
{
	PBUFFER_OBJ pBuffer = m_pBufferHead->pNext;
	
	//�ҵ����˼���socket�������socket
	while (pBuffer)
	{
		if (pBuffer->pSocket->lpfnAcceptEx == nullptr)
		{
			m_strSendBuf.clear();
			int32_t datalength = ( int32_t ) strBuffer.length();

			//�����ͷ
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