#pragma once

#include <thread>
#include <mutex>
#include <string>
#include <memory>
#include <condition_variable>
#include <stdint.h>
#include <winsock2.h>
#pragma comment(lib,"Ws2_32.lib")

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

//网络层负责数据得传输和接收
class CIUSocket
{
private:
	CIUSocket();
	~CIUSocket();

	CIUSocket(const CIUSocket& rhs) = delete;
	CIUSocket& operator = (const CIUSocket& rhs) = delete;

public:
	static CIUSocket& GetInstance();

	/**
	 * @brief  初始化socket，创建发送消息线程和接受消息线程
	 * @param[in] nPort  端口
	 * @return 无
	 */
	bool Init(int nPort);

	/**
	 * @brief 反初始化socket，清理发送消息线程和接受消息线程
	 * @return 无
	 */
	void Uninit();

	//异步接口
	/**
	 * @brief 反初始化socket，清理发送消息线程和接受消息线程
	 * @param[in] strBuffer 消息内容
	 * @param[in] nAppType  微应用类型
	 * @param[in] nCmd		命令
	 * @return 无
	 */
	void Send(const std::string& strBuffer, AppType nAppType, Cmd nCmd);

	//同步接口

private:
	/**
	 * @brief 发送消息线程处理函数
	 * @return 无
	 */
	void SendThreadProc();

	/**
	 * @brief 接受消息线程处理函数
	 * @return 无
	 */
	void RecvThreadProc();

	/**
	 * @brief 通过socket发送数据
	 * @return 结果
	 */
	bool Send();

	/**
	 * @brief 通过socket接受数据
	 * @return 结果
	 */
	bool Recv();

	/**
	 * @brief 发送nBuffSize长度的字节，如果发不出去，则就认为失败
	 * @param[in] pBuffer    消息内容
	 * @param[in] nBuffSize  消息长度
	 * @param[in] nTimeout   超时时间
	 * @return 结果
	 */
	bool SendData(const char* pBuffer, int nBuffSize, int nTimeout);

	/**
	 * @brief 收取nBufferSize长度的字节，如果收不到，则认为失败
	 * @param[in] pBuffer    消息内容
	 * @param[in] nBuffSize  消息长度
	 * @param[in] nTimeout   超时时间
	 * @return 结果
	 */
	bool RecvData(char* pszBuff, int nBufferSize, int nTimeout);

	/**
	* @brief 判断Socket上是否收到数据
	* @return -1出错 0无数据 1有数据
	*/
	int CheckReceivedData();

	/**
	 * @brief 解包
	 * @return 结果
	 */
	bool DecodePackages();

	/**
	 * @brief 等待线程结束
	 * @return 无
	 */
	void Join();

	/**
	 * @brief 关闭socket
	 * @return 无
	 */
	void Close();

	/**
	 * @brief 返回socket连接状态
	 * @return bool
	 */
	bool IsClosed();

	/**
	 * @brief socket连接
	 * @param[in] timeout  超时时间，默认为3秒
	 * @return 无
	 */
	bool Connect(int timeout = 3);

private:
	SOCKET							m_hSocket;				//一般用途Socket（非阻塞socket）

	std::unique_ptr<std::thread>    m_spSendThread;
	std::unique_ptr<std::thread>    m_spRecvThread;

	std::string						m_strServer;			//服务器地址 127.0.0.1

	std::string                     m_strSendBuf;
	std::string                     m_strRecvBuf;

	std::condition_variable         m_cvSendBuf;			
	std::condition_variable         m_cvRecvBuf;

	std::mutex                      m_mtSendBuf;			//m_strSendBuf 锁

	short							m_nPort;				//端口号

	bool							m_bConnected;			//socket 连接状态
	bool                            m_bStop;				//停止标志

	CInitSock m_InitSock;
};

