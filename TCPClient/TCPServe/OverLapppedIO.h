#pragma once

#include <winsock2.h>
#include <Mswsock.h>
#include <stdio.h>
#include <thread>
#include <string>
#include <memory>
#include <windows.h>

#pragma comment(lib, "WS2_32")	// ���ӵ�WS2_32.lib

#pragma pack(push, 1)

//Э��ͷ
struct DataHeader
{
	char commpress;
	int32_t dataLength;		//����Ĵ�С
	int32_t appType;		//App ����
	int32_t cmd;			//����
};

struct GetAppCode
{
	short ret;		//���
	short appMicrocode;	//appcode
};

struct ExitApp
{
	short ret;		//���
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

//�Զ����׽��ֶ���
typedef struct _SOCKET_OBJ
{
	SOCKET s;						// �׽��־��
	int nOutstandingOps;			// ��¼���׽����ϵ��ص�I/O����

	LPFN_ACCEPTEX lpfnAcceptEx;		// ��չ����AcceptEx��ָ�루���Լ����׽��ֶ��ԣ�
} SOCKET_OBJ, * PSOCKET_OBJ;

//�Զ��建��������
typedef struct _BUFFER_OBJ
{
	OVERLAPPED ol;			// �ص��ṹ
	char* buff;				// send/recv/AcceptEx��ʹ�õĻ�����
	int nLen;				// buff�ĳ���
	PSOCKET_OBJ pSocket;	// ��I/O�������׽��ֶ���

	int nOperation;			// �ύ�Ĳ�������

#define OP_ACCEPT	1
#define OP_READ		2
#define OP_WRITE	3

	SOCKET sAccept;			// ��������AcceptEx���ܵĿͻ��׽��֣����Լ����׽��ֶ��ԣ�
	_BUFFER_OBJ* pNext;
} BUFFER_OBJ, * PBUFFER_OBJ;

class CInitSock
{
public:
	CInitSock(BYTE minorVer = 2, BYTE majorVer = 2)
	{
		// ��ʼ��WS2_32.dll
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
	* @brief ��ʼ��
	* @return void
	*/
	void Init();

	/**
	* @brief ����ʼ��
	* @return void
	*/
	void Uninit();

	void SendData(const std::string& strBuffer, AppType nAppType, Cmd nCmd);
private:
	/**
	* @brief ����һ���׽��ֶ��󣬳�ʼ����Ա
	* @param[in] s SOCKET���
	* @return PSOCKET_OBJ �׽��ֶ���
	*/
	PSOCKET_OBJ GetSocketObj(SOCKET s);

	/**
	* @brief �ͷ�һ���׽��ֶ���
	* @param[in] pSocket �׽���ָ��
	* @return void
	*/
	void FreeSocketObj(PSOCKET_OBJ pSocket);

	/**
	* @brief ����һ������������
	* @param[in] pSocket �׽��ֵ�ַ
	* @param[in] nLen ��ַ�ĳ���
	* @return PBUFFER_OBJ ������ָ��
	*/
	PBUFFER_OBJ GetBufferObj(PSOCKET_OBJ pSocket, ULONG nLen);

	/**
	* @brief �ͷ�һ������������
	* @param[in] pBuffer ��������ַ
	* @return void
	*/
	void FreeBufferObj(PBUFFER_OBJ pBuffer);

	/**
	* @brief ͨ���¼������ڻ������б��в���BUFFER_OBJ����
	* @param[in] hEvent �¼�����
	* @return PBUFFER_OBJ ��������ַ
	*/
	PBUFFER_OBJ FindBufferObj(HANDLE hEvent);

	/**
	* @brief �����¼��������
	* @return void
	*/
	void RebuildArray();

	/**
	* @brief �ύ�������ӵ�BUFFER_OBJ����
	* @param[in] pBuffer �����������ַ
	* @return BOOL ���
	*/
	BOOL PostAccept(PBUFFER_OBJ pBuffer);

	/**
	* @brief �ύ�������ݵ�BUFFER_OBJ����
	* @param[in] pBuffer �����������ַ
	* @return BOOL ���
	*/
	BOOL PostRecv(PBUFFER_OBJ pBuffer);

	/**
	* @brief �ύ�������ݵ�BUFFER_OBJ����
	* @param[in] pBuffer �����������ַ
	* @return BOOL ���
	*/
	BOOL PostSend(PBUFFER_OBJ pBuffer);

	/**
	* @brief IO������ɺ󣬴�����
	* @param[in] pBuffer �����������ַ
	* @return BOOL ���
	*/
	BOOL HandleIO(PBUFFER_OBJ pBuffer);

	/**
	* @brief �����߳�
	* @return void
	*/
	void ServiceProc(void);

	/**
	 * @brief ���
	 * @return ���
	 */
	bool DecodePackages();

private:
	short							m_nPort;				//�˿ں�
	bool                            m_bStop;				//ֹͣ��־

	int								m_nBufferCount;			//����������Ч�������
	HANDLE							m_events[WSA_MAXIMUM_WAIT_EVENTS];	// I/O�¼��������
	
	PBUFFER_OBJ						m_pBufferHead;			// ��¼������������ɵı�ĵ�ַ
	PBUFFER_OBJ						m_pBufferTail;

	CInitSock						m_InitSock;
	std::string						m_strServer;			//��������ַ 127.0.0.1
	std::unique_ptr<std::thread>    m_spServiceThread;

	std::string m_strRecvBuf;
	std::string m_strSendBuf;
};


