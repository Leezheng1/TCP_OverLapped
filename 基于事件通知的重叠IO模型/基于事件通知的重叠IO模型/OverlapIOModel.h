#pragma once
#include "Socket.h"

#define MSGSIZE 1024

/************************************************************************/
/* 注意，此处的 WSAOVERLAPPED 不一定要在首部，区分和完成例程的区别（完成例程中有解释） 
/************************************************************************/
typedef struct
{
	WSAOVERLAPPED overlap;		// 这里保存的一个事件对象...缓冲区的位置
	WSABUF wsaBuf;				// 指明缓冲区的成员 
	char szMessage[MSGSIZE];	// 真正的缓冲区
	DWORD NumberOfBytesRecvd;	// 接收到的字节
	DWORD Flags;				// 是否成功接收
	SOCKADDR_IN addr;			// 客户端信息
	SOCKET sock;				// 套接字
}PER_IO_OPERATION_DATA, *LPPER_IO_OPERATION_DATA;


typedef void(*NetCallBack) (DWORD id, void* param, int len); // 定义消息处理函数指针


/************************************************************************/
/* 基于事件通知的重叠IO模型类 
/* Socket为自己封装的socket类
/************************************************************************/
class OverlapIOModel : public Socket
{
private:
	int g_iTotalConn;									// 总的连接数
	WSAEVENT g_CliEventArr[MAXIMUM_WAIT_OBJECTS];		// 事件对象数组
	LPPER_IO_OPERATION_DATA g_pPerIODataArr[MAXIMUM_WAIT_OBJECTS];	// 重叠结构数组

protected:
	// 消息处理函数
	NetCallBack NetFunc;	

	//begin accept
	void _BeginAccept();

	//clean client socket
	void Cleanup(int index);

	//service proc
	static DWORD WINAPI ServiceProc(LPARAM lparam);

public:
	OverlapIOModel();
	~OverlapIOModel();	

	//init net
	BOOL InitNet(NetCallBack func, UINT nSocketPOrt, LPCSTR lpszSocketAddr = ADDR_ANY);	
	
	//wsa send to client
	bool WSASendToClient(DWORD id, void* lparam);
};

