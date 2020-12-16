#pragma once

#include <winsock2.h>
#include <Mswsock.h>
#include <stdio.h>
#include <thread>
#include <string>
#include <memory>
#include <windows.h>

#pragma comment(lib, "WS2_32")	// 链接到WS2_32.lib

#pragma pack(push, 1)

//协议头
struct DataHeader
{
	char commpress;
	int32_t dataLength;		//包体的大小
	int32_t appType;		//App 类型
	int32_t cmd;			//命令
};

struct GetAppCode
{
	short ret;		//结果
	short appMicrocode;	//appcode
};

struct ExitApp
{
	short ret;		//结果
	short appcode;	//appcode
};

#pragma pack(pop)

enum AppType
{
	AppType_CloudDesktop,
};

enum Cmd
{
	Cmd_GetMicroCode,
	Cmd_Exit,
};

#define BUFFER_SIZE 1024

//自定义套接字对象
typedef struct _SOCKET_OBJ
{
	SOCKET s;						// 套接字句柄
	int nOutstandingOps;			// 记录此套接字上的重叠I/O数量

	LPFN_ACCEPTEX lpfnAcceptEx;		// 扩展函数AcceptEx的指针（仅对监听套接字而言）
} SOCKET_OBJ, * PSOCKET_OBJ;

//自定义缓冲区对象
typedef struct _BUFFER_OBJ
{
	OVERLAPPED ol;			// 重叠结构
	char* buff;				// send/recv/AcceptEx所使用的缓冲区
	int nLen;				// buff的长度
	PSOCKET_OBJ pSocket;	// 此I/O所属的套接字对象

	int nOperation;			// 提交的操作类型

#define OP_ACCEPT	1
#define OP_READ		2
#define OP_WRITE	3

	SOCKET sAccept;			// 用来保存AcceptEx接受的客户套接字（仅对监听套接字而言）
	_BUFFER_OBJ* pNext;
} BUFFER_OBJ, * PBUFFER_OBJ;

class CInitSock
{
public:
	CInitSock(BYTE minorVer = 2, BYTE majorVer = 2)
	{
		// 初始化WS2_32.dll
		WSADATA wsaData;
		WORD sockVersion = MAKEWORD(minorVer, majorVer);
		if (::WSAStartup(sockVersion, &wsaData) != 0)
		{
			exit(0);
		}
	}
	~CInitSock()
	{
		::WSACleanup();
	}
};

class COverLapppedIO
{
private:
	COverLapppedIO();
	~COverLapppedIO();

	COverLapppedIO(const COverLapppedIO& rhs) = delete;
	COverLapppedIO& operator = (const COverLapppedIO& rhs) = delete;

public:
	static COverLapppedIO& GetInstance();

	/**
	* @brief 初始化
	* @return void
	*/
	void Init();

	/**
	* @brief 反初始化
	* @return void
	*/
	void Uninit();

	void SendData(const std::string& strBuffer, AppType nAppType, Cmd nCmd);
private:
	/**
	* @brief 申请一个套接字对象，初始化成员
	* @param[in] s SOCKET句柄
	* @return PSOCKET_OBJ 套接字对象
	*/
	PSOCKET_OBJ GetSocketObj(SOCKET s);

	/**
	* @brief 释放一个套接字对象
	* @param[in] pSocket 套接字指针
	* @return void
	*/
	void FreeSocketObj(PSOCKET_OBJ pSocket);

	/**
	* @brief 申请一个缓冲区对象
	* @param[in] pSocket 套接字地址
	* @param[in] nLen 地址的长度
	* @return PBUFFER_OBJ 缓冲区指针
	*/
	PBUFFER_OBJ GetBufferObj(PSOCKET_OBJ pSocket, ULONG nLen);

	/**
	* @brief 释放一个缓冲区对象
	* @param[in] pBuffer 缓冲区地址
	* @return void
	*/
	void FreeBufferObj(PBUFFER_OBJ pBuffer);

	/**
	* @brief 通过事件对象在缓冲区列表中查找BUFFER_OBJ对象
	* @param[in] hEvent 事件对象
	* @return PBUFFER_OBJ 缓冲区地址
	*/
	PBUFFER_OBJ FindBufferObj(HANDLE hEvent);

	/**
	* @brief 更新事件句柄数组
	* @return void
	*/
	void RebuildArray();

	/**
	* @brief 提交接受连接的BUFFER_OBJ对象
	* @param[in] pBuffer 缓冲区对象地址
	* @return BOOL 结果
	*/
	BOOL PostAccept(PBUFFER_OBJ pBuffer);

	/**
	* @brief 提交接受数据的BUFFER_OBJ对象
	* @param[in] pBuffer 缓冲区对象地址
	* @return BOOL 结果
	*/
	BOOL PostRecv(PBUFFER_OBJ pBuffer);

	/**
	* @brief 提交发送数据的BUFFER_OBJ对象
	* @param[in] pBuffer 缓冲区对象地址
	* @return BOOL 结果
	*/
	BOOL PostSend(PBUFFER_OBJ pBuffer);

	/**
	* @brief IO请求完成后，处理函数
	* @param[in] pBuffer 缓冲区对象地址
	* @return BOOL 结果
	*/
	BOOL HandleIO(PBUFFER_OBJ pBuffer);

	/**
	* @brief 服务线程
	* @return void
	*/
	void ServiceProc(void);

	/**
	 * @brief 解包
	 * @return 结果
	 */
	bool DecodePackages();

private:
	short							m_nPort;				//端口号
	bool                            m_bStop;				//停止标志

	int								m_nBufferCount;			//下数组中有效句柄数量
	HANDLE							m_events[WSA_MAXIMUM_WAIT_EVENTS];	// I/O事件句柄数组
	
	PBUFFER_OBJ						m_pBufferHead;			// 记录缓冲区对象组成的表的地址
	PBUFFER_OBJ						m_pBufferTail;

	CInitSock						m_InitSock;
	std::string						m_strServer;			//服务器地址 127.0.0.1
	std::unique_ptr<std::thread>    m_spServiceThread;

	std::string m_strRecvBuf;
	std::string m_strSendBuf;
};


