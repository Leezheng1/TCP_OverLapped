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

//����㸺�����ݵô���ͽ���
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
	 * @brief  ��ʼ��socket������������Ϣ�̺߳ͽ�����Ϣ�߳�
	 * @param[in] nPort  �˿�
	 * @return ��
	 */
	bool Init(int nPort);

	/**
	 * @brief ����ʼ��socket����������Ϣ�̺߳ͽ�����Ϣ�߳�
	 * @return ��
	 */
	void Uninit();

	//�첽�ӿ�
	/**
	 * @brief ����ʼ��socket����������Ϣ�̺߳ͽ�����Ϣ�߳�
	 * @param[in] strBuffer ��Ϣ����
	 * @param[in] nAppType  ΢Ӧ������
	 * @param[in] nCmd		����
	 * @return ��
	 */
	void Send(const std::string& strBuffer, AppType nAppType, Cmd nCmd);

	//ͬ���ӿ�

private:
	/**
	 * @brief ������Ϣ�̴߳�����
	 * @return ��
	 */
	void SendThreadProc();

	/**
	 * @brief ������Ϣ�̴߳�����
	 * @return ��
	 */
	void RecvThreadProc();

	/**
	 * @brief ͨ��socket��������
	 * @return ���
	 */
	bool Send();

	/**
	 * @brief ͨ��socket��������
	 * @return ���
	 */
	bool Recv();

	/**
	 * @brief ����nBuffSize���ȵ��ֽڣ����������ȥ�������Ϊʧ��
	 * @param[in] pBuffer    ��Ϣ����
	 * @param[in] nBuffSize  ��Ϣ����
	 * @param[in] nTimeout   ��ʱʱ��
	 * @return ���
	 */
	bool SendData(const char* pBuffer, int nBuffSize, int nTimeout);

	/**
	 * @brief ��ȡnBufferSize���ȵ��ֽڣ�����ղ���������Ϊʧ��
	 * @param[in] pBuffer    ��Ϣ����
	 * @param[in] nBuffSize  ��Ϣ����
	 * @param[in] nTimeout   ��ʱʱ��
	 * @return ���
	 */
	bool RecvData(char* pszBuff, int nBufferSize, int nTimeout);

	/**
	* @brief �ж�Socket���Ƿ��յ�����
	* @return -1���� 0������ 1������
	*/
	int CheckReceivedData();

	/**
	 * @brief ���
	 * @return ���
	 */
	bool DecodePackages();

	/**
	 * @brief �ȴ��߳̽���
	 * @return ��
	 */
	void Join();

	/**
	 * @brief �ر�socket
	 * @return ��
	 */
	void Close();

	/**
	 * @brief ����socket����״̬
	 * @return bool
	 */
	bool IsClosed();

	/**
	 * @brief socket����
	 * @param[in] timeout  ��ʱʱ�䣬Ĭ��Ϊ3��
	 * @return ��
	 */
	bool Connect(int timeout = 3);

private:
	SOCKET							m_hSocket;				//һ����;Socket��������socket��

	std::unique_ptr<std::thread>    m_spSendThread;
	std::unique_ptr<std::thread>    m_spRecvThread;

	std::string						m_strServer;			//��������ַ 127.0.0.1

	std::string                     m_strSendBuf;
	std::string                     m_strRecvBuf;

	std::condition_variable         m_cvSendBuf;			
	std::condition_variable         m_cvRecvBuf;

	std::mutex                      m_mtSendBuf;			//m_strSendBuf ��

	short							m_nPort;				//�˿ں�

	bool							m_bConnected;			//socket ����״̬
	bool                            m_bStop;				//ֹͣ��־

	CInitSock m_InitSock;
};

