#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "OverlapIOModel.h"
#include <stdio.h>


OverlapIOModel::OverlapIOModel()
{
	g_iTotalConn = 0;
}


OverlapIOModel::~OverlapIOModel()
{
}


/************************************************************************/
/* �����¼�֪ͨ���ص�IO����ģ�͵���Ҫʵ�֣������̣߳�                        */
/************************************************************************/
DWORD WINAPI OverlapIOModel::ServiceProc(LPARAM lparam)
{
	OverlapIOModel* pOverLapIO = (OverlapIOModel*)lparam;
	int ret = 0;
	int index = 0;
	DWORD err = 0;
	while (TRUE)
	{
		//////////////////////////////////////////////////////////////////////////
		// �ȴ�����¼��������źŷ���
		ret = WSAWaitForMultipleEvents(pOverLapIO->g_iTotalConn, pOverLapIO->g_CliEventArr, FALSE, 1000, FALSE);
		if (ret == WSA_WAIT_FAILED || ret == WSA_WAIT_TIMEOUT)
			continue;

		index = ret - WSA_WAIT_EVENT_0; // ��ȡ����ֵ���˴� WSA_WAIT_EVENT_0 ��ʵ�͵���0...
		
		//////////////////////////////////////////////////////////////////////////
		// ���¼�������Ϊ���ź�״̬
		if (FALSE == WSAResetEvent(pOverLapIO->g_CliEventArr[index])) 
		{
			pOverLapIO->Cleanup(index);
			continue;
		}

		//////////////////////////////////////////////////////////////////////////
		// ��ȡ�ص������Ľ�����������FALSE����ʾ�ص�����δ���
		if (FALSE == WSAGetOverlappedResult(  
			pOverLapIO->g_pPerIODataArr[index]->sock,
			&(pOverLapIO->g_pPerIODataArr[index]->overlap),
			&(pOverLapIO->g_pPerIODataArr[index]->NumberOfBytesRecvd),
			TRUE,
			&(pOverLapIO->g_pPerIODataArr[index]->Flags)))
		{
			printf("WSAGetOverlappedResult failed:%d\n", WSAGetLastError());
			continue;
		}

		//////////////////////////////////////////////////////////////////////////
		// ���յ����ص�������ֽ���Ϊ0�ǣ���ʾ�ͻ��˶Ͽ�����
		if (pOverLapIO->g_pPerIODataArr[index]->NumberOfBytesRecvd == 0)
		{
			printf("[%s:%d]->log off\n", 
				inet_ntoa(pOverLapIO->g_pPerIODataArr[index]->addr.sin_addr),
				ntohs(pOverLapIO->g_pPerIODataArr[index]->addr.sin_port));
			pOverLapIO->Cleanup(index);
		}
		else
		{
			//////////////////////////////////////////////////////////////////////////
			// �����ݽ��յ��������ݽ��д���NetFunc��������Ͷ��һ���첽����WSARecv��
			// ��Ϊ WSARecv ֻ��һ����Ч�Ļ��ᣬ���������һ�β�����֮�󣬻��н������Ĳ����ͻ�Ҫ����Ͷ��һ��WSARecv
			pOverLapIO->NetFunc(pOverLapIO->g_pPerIODataArr[index]->sock,
				pOverLapIO->g_pPerIODataArr[index]->szMessage,
				pOverLapIO->g_pPerIODataArr[index]->NumberOfBytesRecvd);

			ZeroMemory(pOverLapIO->g_pPerIODataArr[index]->szMessage,
				sizeof(pOverLapIO->g_pPerIODataArr[index]->szMessage));
			pOverLapIO->g_pPerIODataArr[index]->Flags = 0;
			
			if (SOCKET_ERROR == WSARecv( //create new WSARecv
				pOverLapIO->g_pPerIODataArr[index]->sock,
				&(pOverLapIO->g_pPerIODataArr[index]->wsaBuf),
				1,
				&(pOverLapIO->g_pPerIODataArr[index]->NumberOfBytesRecvd),
				&(pOverLapIO->g_pPerIODataArr[index]->Flags),
				&(pOverLapIO->g_pPerIODataArr[index]->overlap),
				NULL))
			{
				err = WSAGetLastError();
				if (WSA_IO_PENDING != err) // WSA_IO_PENDING �ص�IO�����ɹ����ȴ��Ժ����
				{
					HeapFree(GetProcessHeap(), 0, pOverLapIO->g_pPerIODataArr[index]);
					printf("WSARecv failed:%d\n", err);
				};
			}
		}
	}
}


/************************************************************************/
/* ��ʼ�����绷��                                                        */
/************************************************************************/
BOOL OverlapIOModel::InitNet(NetCallBack func, UINT nSocketPOrt, LPCSTR lpszSocketAddr /*= ADDR_ANY*/)
{
	NetFunc = func;
	if (!Create(nSocketPOrt, SOCK_STREAM, lpszSocketAddr))
		return false;
	if (!Listen(5))
		return false;
	CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)ServiceProc, this, NULL, NULL);
	_BeginAccept();
	return true;
}


/************************************************************************/
/* ���տͻ��˵�����                                                       */
/************************************************************************/
void OverlapIOModel::_BeginAccept()
{
	SOCKET sClient = 0;
	SOCKADDR_IN addrClient = { 0 };
	int nLen = sizeof(SOCKADDR_IN);
	int err = 0;

	while (true)
	{
		sClient = accept(m_hSocket, (sockaddr*)&addrClient, &nLen);
		if (sClient == INVALID_SOCKET)
			continue;

		printf("[%s:%d]->log on\n", inet_ntoa(addrClient.sin_addr), ntohs(addrClient.sin_port));

		g_pPerIODataArr[g_iTotalConn] = (LPPER_IO_OPERATION_DATA)HeapAlloc(// �ڳ����Ĭ�϶��������ڴ�
			GetProcessHeap(),
			HEAP_ZERO_MEMORY,			
			sizeof(PER_IO_OPERATION_DATA));
		g_pPerIODataArr[g_iTotalConn]->sock = sClient;
		g_pPerIODataArr[g_iTotalConn]->addr = addrClient;
		g_pPerIODataArr[g_iTotalConn]->NumberOfBytesRecvd = 0;
		g_pPerIODataArr[g_iTotalConn]->wsaBuf.len = MSGSIZE;
		g_pPerIODataArr[g_iTotalConn]->wsaBuf.buf = g_pPerIODataArr[g_iTotalConn]->szMessage;
		//create net event
		g_CliEventArr[g_iTotalConn] = g_pPerIODataArr[g_iTotalConn]->overlap.hEvent = WSACreateEvent();

		//begin wsa recv(only can recv one time)
		if (SOCKET_ERROR == WSARecv(
			g_pPerIODataArr[g_iTotalConn]->sock,
			&(g_pPerIODataArr[g_iTotalConn]->wsaBuf),
			1,
			&(g_pPerIODataArr[g_iTotalConn]->NumberOfBytesRecvd),
			&(g_pPerIODataArr[g_iTotalConn]->Flags),
			&(g_pPerIODataArr[g_iTotalConn]->overlap),
			NULL))
		{
			err = WSAGetLastError();
			if (WSA_IO_PENDING != err) // WSA_IO_PENDING �ص�IO�����ɹ����ȴ��Ժ����
			{
				HeapFree(GetProcessHeap(), 0, g_pPerIODataArr[g_iTotalConn]);
				printf("WSARecv failed:%d\n", err);
				continue;
			};
		}
		g_iTotalConn++;
	}
}


/************************************************************************/
/* ������Ϣ���͵��첽Ͷ��                                                 */
/************************************************************************/
bool OverlapIOModel::WSASendToClient(DWORD id, void* lparam)
{
	char* buf = (char*)lparam;
	int dwRet = 0;	
	WSABUF wsaBuf;
	wsaBuf.len = strlen(buf);
	wsaBuf.buf = buf;
	DWORD dwSendBytes = 0;
	DWORD Flags = 0;
	WSAOVERLAPPED overlap;
	overlap.hEvent = WSACreateEvent();

	// Ͷ��һ��WSASend������ϵͳ�������������������Ϣ�ķ���
	if (SOCKET_ERROR == WSASend(id, &wsaBuf, 1, &dwSendBytes, Flags, &overlap, NULL))
	{
		dwRet = WSAGetLastError();
		if (dwRet != WSA_IO_PENDING)
		{
			printf("WSASend failed:%d\n", dwRet);
			return false;
		}
	}
	WSACloseEvent(overlap.hEvent);// �Է��͵���Ϣû���¼��ļ�⣬����������ں���Դ

	return true;
}


/************************************************************************/
/* ���һ���ͻ����׽���                                                   */
/************************************************************************/
void OverlapIOModel::Cleanup(int index)
{
	if (SOCKET_ERROR == closesocket(g_pPerIODataArr[index]->sock))
		printf("closesocket failed:%d\n", GetLastError());
	if (0 == HeapFree(GetProcessHeap(), 0, g_pPerIODataArr[index])) 
		printf("HeapFree failed:%d\n", GetLastError());
	if (index < g_iTotalConn - 1)
	{
		g_CliEventArr[index] = g_CliEventArr[g_iTotalConn - 1];
		g_pPerIODataArr[index] = g_pPerIODataArr[g_iTotalConn - 1];
	}
	g_pPerIODataArr[--g_iTotalConn] = NULL;
}
