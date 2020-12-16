#define _WINSOCK_DEPRECATED_NO_WARNINGS 1

#include "IUSocket.h"
#include <functional>
#include <Sensapi.h>

#pragma comment(lib, "Sensapi.lib")//������ͨ�Լ��


//������ֽ�������Ϊ10M
#define MAX_PACKAGE_SIZE    10 * 1024 * 1024


CIUSocket::CIUSocket()
{
    m_hSocket = INVALID_SOCKET;
    m_strServer = "127.0.0.1";

    m_bStop = false;
    m_bConnected = false;
}

CIUSocket::~CIUSocket()
{
}

CIUSocket& CIUSocket::GetInstance()
{
    static CIUSocket socketInstance;
    return socketInstance;
}

bool CIUSocket::Init(int nPort)
{
    m_bStop = false;
    m_nPort = nPort;
    //Connect();

    if (!m_spSendThread)
        m_spSendThread.reset(new std::thread(std::bind(&CIUSocket::SendThreadProc, this)));

    if (!m_spRecvThread)
        m_spRecvThread.reset(new std::thread(std::bind(&CIUSocket::RecvThreadProc, this)));

    return true;
}

void CIUSocket::Uninit()
{
    m_bStop = true;
    Close();

    if (m_spSendThread)
        m_spSendThread->detach();
    if (m_spRecvThread)
        m_spRecvThread->detach();

    //����߳��˵��ˣ�����ָ����Զ�Ϊ�գ������ٴ�reset()ʱ���ж�һ��
    if (m_spSendThread)
        m_spSendThread.reset();
    if (m_spRecvThread)
        m_spRecvThread.reset();

    Join();
}

void CIUSocket::Join()
{
    if (m_spSendThread && m_spSendThread->joinable())
        m_spSendThread->join();
    if (m_spRecvThread && m_spRecvThread->joinable())
        m_spRecvThread->join();
}

bool CIUSocket::Connect(int timeout)
{
    Close();

    //TCP ���ӣ��ṩ���л����ɿ��ء�˫�����ӵ��ֽ�����
    m_hSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_hSocket == INVALID_SOCKET)
        return false;

    long tmSend = 3 * 1000L;
    long tmRecv = 3 * 1000L;
    long noDelay = 1;

    //����TCP_NODELAY������ζ�Ž�����Nagle�㷨������С���ķ��͡�������ʱ�����ͣ�ͬʱ���ݴ������Ƚ�С��Ӧ�ã�����TCP_NODELAYѡ��������һ����ȷ��ѡ��
    setsockopt(m_hSocket, IPPROTO_TCP, TCP_NODELAY, ( LPSTR ) &noDelay, sizeof(long));

    setsockopt(m_hSocket, SOL_SOCKET, SO_SNDTIMEO, ( LPSTR ) &tmSend, sizeof(long));   //���ý��ܵĳ�ʱʱ��
    setsockopt(m_hSocket, SOL_SOCKET, SO_RCVTIMEO, ( LPSTR ) &tmRecv, sizeof(long));   //���÷��͵ĳ�ʱʱ��

    //��socket���óɷ������� ����1 
    unsigned long on = 1;
    if (::ioctlsocket(m_hSocket, FIONBIO, &on) == SOCKET_ERROR)
        return false;

    struct sockaddr_in addrSrv = { 0 };
    struct hostent* pHostent = NULL;
    unsigned int addr = 0;

    addrSrv.sin_addr.s_addr = inet_addr(m_strServer.c_str());
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons(( u_short ) m_nPort);
    int ret = ::connect(m_hSocket, (struct sockaddr*) & addrSrv, sizeof(addrSrv));
    if (ret == 0)
    {
        OutputDebugString(L"Connect to server:%s, port:%d successfully.");
        m_bConnected = true;
        return true;
    } 
    else if (ret == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        OutputDebugString(L"Could not connect to server:%s, port:%d");
        return false;
    }

    fd_set writeset;
    FD_ZERO(&writeset);                 //��д�׽��ּ������
    FD_SET(m_hSocket, &writeset);       //����һ��д�ļ�������
    struct timeval tv = { timeout, 0 }; //����select�ĳ�ʱʱ��

    //�����������������������׽��ֵ�����
    if (::select(m_hSocket + 1, NULL, &writeset, NULL, &tv) != 1)
    {
        OutputDebugString(L"Could not connect to server:%s, port:%d");
        return false;
    }

    m_bConnected = true;

    return true;
}

int CIUSocket::CheckReceivedData()
{
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(m_hSocket, &readset);

    fd_set exceptionset;
    FD_ZERO(&exceptionset);
    FD_SET(m_hSocket, &exceptionset);

    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500;

    long nRet = ::select(m_hSocket + 1, &readset, NULL, &exceptionset, &timeout);
    if (nRet >= 1)
    {
        if (FD_ISSET(m_hSocket, &exceptionset))     //����
            return -1;

        if (FD_ISSET(m_hSocket, &readset))          //������
            return 1;
    }
    //����
    else if (nRet == SOCKET_ERROR)                  //����
        return -1;

    //��ʱnRet=0���ڳ�ʱ�����ʱ����û������
    return 0;
}

//�ú�����Ҫ�������߳��е��ã������m_spSendThread��
void CIUSocket::Send(const std::string& strBuffer, AppType nAppType, Cmd nCmd)
{
    std::string strDestBuf;
   
    //�����ͷ
    int32_t length = ( int32_t ) strBuffer.length();


    //TODO  �����Ϣ����
    DataHeader header ={};
    memset(&header, 0, sizeof(header));

    header.commpress = 0;
    header.dataLength = length;
    header.appType = nAppType;
    header.cmd = nCmd;

    std::lock_guard<std::mutex> guard(m_mtSendBuf);
    m_strSendBuf.append(( const char* ) &header, sizeof(header));

    m_strSendBuf.append(strBuffer);
    m_cvSendBuf.notify_one();
}

void CIUSocket::SendThreadProc()
{
    while (!m_bStop)
    {
        //�ȴ�ǰ�ȼ���
        std::unique_lock<std::mutex> guard(m_mtSendBuf);
        while (m_strSendBuf.empty())
        {
            if (m_bStop)
                return;

            //��ס��ǰ�̣߳��ȴ����������������ȴ�ʱ�����������㣬wait��ԭ���ԵĽ������ѵ�ǰ�̹߳���
            m_cvSendBuf.wait(guard);
        }

        if (!Send())
        {
            //����������������Ӳ��ϣ�����ͻ��˱������
            return;
        }
    }
}

bool CIUSocket::Send()
{
    //���δ����������������Ҳʧ���򷵻�FALSE
    //TODO: �ڷ������ݵĹ���������ûʲô���壬��Ϊ������Session�Ѿ���Ч�ˣ������ط�����
    if (IsClosed() && !Connect())
    {
        OutputDebugString(L"connect server:%s:%d error.");
        return false;
    }

    int nSentBytes = 0;
    int nRet = 0;
    while (true)
    {
        nRet = ::send(m_hSocket, m_strSendBuf.c_str(), m_strSendBuf.length(), 0);
        if (nRet == SOCKET_ERROR)
        {
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            else
            {
                OutputDebugString(L"Send data error, disconnect server:%s, port:%d.");

                Close();
                return false;
            }
        } else if (nRet < 1)
        {
            //һ�����ִ�������̹ر�Socket
            OutputDebugString(L"Send data error, disconnect server:%s, port:%d.");
            Close();
            return false;
        }

        m_strSendBuf.erase(0, nRet);
        if (m_strSendBuf.empty())
            break;

        ::Sleep(1);
    }

    return true;
}

void CIUSocket::RecvThreadProc()
{
    int nRet;
    //������ʽ 
    DWORD   dwFlags;
    BOOL    bAlive;
    while (!m_bStop)
    {
        //��⵽������������
        nRet = CheckReceivedData();
        //����
        if (nRet == -1)
        {
            //
        }
        //������
        else if (nRet == 0)
        {
            bAlive = ::IsNetworkAlive(&dwFlags);		//�Ƿ�����    
            if (!bAlive && ::GetLastError() == 0)
            {
                //�����Ѿ��Ͽ�
                Uninit();
                break;
            }
        }
        //������
        else if (nRet == 1)
        {
            printf("���յ�����\n");
            if (!Recv())
            {
               //ʧ��
                continue;
            }

            DecodePackages();
        }// end if

        Sleep(100);

    }// end while-loop
}

bool CIUSocket::Recv()
{
    int nRet = 0;
    char buff[10 * 1024];
    while (true)
    {
        nRet = ::recv(m_hSocket, buff, 10 * 1024, 0);
        if (nRet == SOCKET_ERROR)				//һ�����ִ�������̹ر�Socket
        {
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            else
            {
                OutputDebugString(L"Recv data error, errorNO=%d.");
                //Close();
                return false;
            }
        } else if (nRet < 1)
        {
            OutputDebugString(L"Recv data error, errorNO=%d.");
            //Close();
            return false;
        }

        m_strRecvBuf.append(buff, nRet);

        ::Sleep(1);
    }

    return true;
}

bool CIUSocket::DecodePackages()
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
        printf("RecvData from Server\n");
        printf("header commpress: %d \n", header.commpress);
        printf("header dataLength: %d\n", header.dataLength);
        printf("header appType: %d \n", header.appType);
        printf("header cmd: %d \n", header.cmd);

        printf("content: [%s] \n", strBody.c_str());

        printf("\n----------------------------------------- \n");

        //TODO
        //�ص�֪ͨ
        
    }// end while

    return true;
}

bool CIUSocket::SendData(const char* pBuffer, int nBuffSize, int nTimeout)
{
    //TODO������ط������ȼӸ�select�ж���socket�Ƿ��д

    int64_t nStartTime = time(NULL);

    int nSentBytes = 0;
    int nRet = 0;

    //ѭ����������
    while (true)
    {
        nRet = ::send(m_hSocket, pBuffer, nBuffSize, 0);
        if (nRet == SOCKET_ERROR)
        {
            //�Է�tcp����̫С��ʱ������ȥ��ͬʱû�г�ʱ��������ȴ�
            if (::WSAGetLastError() == WSAEWOULDBLOCK && time(NULL) - nStartTime < nTimeout)
            {
                continue;
            } else
                return false;
        } else if (nRet < 1)
        {
            //һ�����ִ�������̹ر�Socket
            //
            Close();
            return false;
        }

        nSentBytes += nRet;
        if (nSentBytes >= nBuffSize)
            break;

        pBuffer += nRet;
        nBuffSize -= nRet;

        ::Sleep(1);
    }

    return true;
}

bool CIUSocket::RecvData(char* pszBuff, int nBufferSize, int nTimeout)
{
    int64_t nStartTime = time(NULL);

    fd_set writeset;
    FD_ZERO(&writeset);
    FD_SET(m_hSocket, &writeset);

    timeval timeout;
    timeout.tv_sec = nTimeout;
    timeout.tv_usec = 0;

    int nRet = ::select(m_hSocket + 1, NULL, &writeset, NULL, &timeout);
    if (nRet != 1)
    {
        Close();
        return false;
    }

    int nRecvBytes = 0;
    int nBytesToRecv = nBufferSize;
    while (true)
    {
        nRet = ::recv(m_hSocket, pszBuff, nBytesToRecv, 0);
        if (nRet == SOCKET_ERROR)				//һ�����ִ�������̹ر�Socket
        {
            if (::WSAGetLastError() == WSAEWOULDBLOCK && time(NULL) - nStartTime < nTimeout)
                continue;
            else
            {
                //LOG_ERROR
                Close();
                return false;
            }
        } else if (nRet < 1)
        {
            //LOG_ERROR
            Close();
            return false;
        }

        nRecvBytes += nRet;
        if (nRecvBytes >= nBufferSize)
            break;

        pszBuff += nRet;
        nBytesToRecv -= nRet;

        ::Sleep(1);
    }

    return true;
}

bool CIUSocket::IsClosed()
{
    return !m_bConnected;
}

void CIUSocket::Close()
{
    //FIXME: ��������ᱻ���ݷ����̺߳���ȡ�߳�ͬʱ���ã�����ȫ
    if (m_hSocket == INVALID_SOCKET)
        return;

    ::shutdown(m_hSocket, SD_BOTH);     //ͬʱ�رս��ܺͷ��Ͳ������ر�����
    ::closesocket(m_hSocket);           //�ر��׽���
    m_hSocket = INVALID_SOCKET;

    m_bConnected = false;
}
