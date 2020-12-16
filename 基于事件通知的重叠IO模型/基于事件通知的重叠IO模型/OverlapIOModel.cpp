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
/* 基于事件通知的重叠IO网络模型的主要实现（工作线程）                        */
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
		// 等待多个事件对象有信号发送
		ret = WSAWaitForMultipleEvents(pOverLapIO->g_iTotalConn, pOverLapIO->g_CliEventArr, FALSE, 1000, FALSE);
		if (ret == WSA_WAIT_FAILED || ret == WSA_WAIT_TIMEOUT)
			continue;

		index = ret - WSA_WAIT_EVENT_0; // 获取索引值，此处 WSA_WAIT_EVENT_0 其实就等于0...
		
		//////////////////////////////////////////////////////////////////////////
		// 将事件对象设为无信号状态
		if (FALSE == WSAResetEvent(pOverLapIO->g_CliEventArr[index])) 
		{
			pOverLapIO->Cleanup(index);
			continue;
		}

		//////////////////////////////////////////////////////////////////////////
		// 获取重叠操作的结果，如果返回FALSE，表示重叠操作未完成
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
		// 接收到的重叠结果的字节数为0是，表示客户端断开连接
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
			// 有数据接收到，对数据进行处理（NetFunc），并再投递一个异步请求（WSARecv）
			// 因为 WSARecv 只有一次生效的机会，如果我们在一次操作完之后，还有接下来的操作就还要继续投递一个WSARecv
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
				if (WSA_IO_PENDING != err) // WSA_IO_PENDING 重叠IO操作成功，等待稍后完成
				{
					HeapFree(GetProcessHeap(), 0, pOverLapIO->g_pPerIODataArr[index]);
					printf("WSARecv failed:%d\n", err);
				};
			}
		}
	}
}


/************************************************************************/
/* 初始化网络环境                                                        */
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
/* 接收客户端的连接                                                       */
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

		g_pPerIODataArr[g_iTotalConn] = (LPPER_IO_OPERATION_DATA)HeapAlloc(// 在程序的默认堆上申请内存
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
			if (WSA_IO_PENDING != err) // WSA_IO_PENDING 重叠IO操作成功，等待稍后完成
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
/* 进行消息发送的异步投递                                                 */
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

	// 投递一个WSASend给操作系统，让它帮助我们完成消息的发送
	if (SOCKET_ERROR == WSASend(id, &wsaBuf, 1, &dwSendBytes, Flags, &overlap, NULL))
	{
		dwRet = WSAGetLastError();
		if (dwRet != WSA_IO_PENDING)
		{
			printf("WSASend failed:%d\n", dwRet);
			return false;
		}
	}
	WSACloseEvent(overlap.hEvent);// 对发送的消息没有事件的检测，所以清除掉内核资源

	return true;
}


/************************************************************************/
/* 清除一个客户端套接字                                                   */
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
