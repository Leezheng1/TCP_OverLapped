#define _WINSOCK_DEPRECATED_NO_WARNINGS 1

#include "IUSocket.h"
#include <functional>
#include <Sensapi.h>

#pragma comment(lib, "Sensapi.lib")//网络连通性检测


//包最大字节数限制为10M
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

    //如果线程退掉了，这里指针会自动为空，所以再次reset()时先判断一下
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

    //TCP 连接，提供序列化、可靠地、双向连接的字节流。
    m_hSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_hSocket == INVALID_SOCKET)
        return false;

    long tmSend = 3 * 1000L;
    long tmRecv = 3 * 1000L;
    long noDelay = 1;

    //启动TCP_NODELAY，就意味着禁用了Nagle算法，允许小包的发送。对于延时敏感型，同时数据传输量比较小的应用，开启TCP_NODELAY选项无疑是一个正确的选择
    setsockopt(m_hSocket, IPPROTO_TCP, TCP_NODELAY, ( LPSTR ) &noDelay, sizeof(long));

    setsockopt(m_hSocket, SOL_SOCKET, SO_SNDTIMEO, ( LPSTR ) &tmSend, sizeof(long));   //设置接受的超时时间
    setsockopt(m_hSocket, SOL_SOCKET, SO_RCVTIMEO, ( LPSTR ) &tmRecv, sizeof(long));   //设置发送的超时时间

    //将socket设置成非阻塞的 设置1 
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
    FD_ZERO(&writeset);                 //将写套接字集合清空
    FD_SET(m_hSocket, &writeset);       //增加一个写文件描述符
    struct timeval tv = { timeout, 0 }; //本次select的超时时间

    //非阻塞，返回满足条件的套接字的数量
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
        if (FD_ISSET(m_hSocket, &exceptionset))     //出错
            return -1;

        if (FD_ISSET(m_hSocket, &readset))          //有数据
            return 1;
    }
    //出错
    else if (nRet == SOCKET_ERROR)                  //出错
        return -1;

    //超时nRet=0，在超时的这段时间内没有数据
    return 0;
}

//该函数需要在其他线程中调用（相比于m_spSendThread）
void CIUSocket::Send(const std::string& strBuffer, AppType nAppType, Cmd nCmd)
{
    std::string strDestBuf;
   
    //插入包头
    int32_t length = ( int32_t ) strBuffer.length();


    //TODO  添加消息类型
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
        //等待前先加锁
        std::unique_lock<std::mutex> guard(m_mtSendBuf);
        while (m_strSendBuf.empty())
        {
            if (m_bStop)
                return;

            //锁住当前线程，等待条件变量触发（等待时，条件不满足，wait会原子性的解锁并把当前线程挂起）
            m_cvSendBuf.wait(guard);
        }

        if (!Send())
        {
            //进行重连，如果连接不上，则向客户端报告错误
            return;
        }
    }
}

bool CIUSocket::Send()
{
    //如果未连接则重连，重连也失败则返回FALSE
    //TODO: 在发送数据的过程中重连没什么意义，因为与服务的Session已经无效了，换个地方重连
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
            //一旦出现错误就立刻关闭Socket
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
    //上网方式 
    DWORD   dwFlags;
    BOOL    bAlive;
    while (!m_bStop)
    {
        //检测到数据则收数据
        nRet = CheckReceivedData();
        //出错
        if (nRet == -1)
        {
            //
        }
        //无数据
        else if (nRet == 0)
        {
            bAlive = ::IsNetworkAlive(&dwFlags);		//是否在线    
            if (!bAlive && ::GetLastError() == 0)
            {
                //网络已经断开
                Uninit();
                break;
            }
        }
        //有数据
        else if (nRet == 1)
        {
            printf("接收到数据\n");
            if (!Recv())
            {
               //失败
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
        if (nRet == SOCKET_ERROR)				//一旦出现错误就立刻关闭Socket
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
        printf("RecvData from Server\n");
        printf("header commpress: %d \n", header.commpress);
        printf("header dataLength: %d\n", header.dataLength);
        printf("header appType: %d \n", header.appType);
        printf("header cmd: %d \n", header.cmd);

        printf("content: [%s] \n", strBody.c_str());

        printf("\n----------------------------------------- \n");

        //TODO
        //回调通知
        
    }// end while

    return true;
}

bool CIUSocket::SendData(const char* pBuffer, int nBuffSize, int nTimeout)
{
    //TODO：这个地方可以先加个select判断下socket是否可写

    int64_t nStartTime = time(NULL);

    int nSentBytes = 0;
    int nRet = 0;

    //循环发送数据
    while (true)
    {
        nRet = ::send(m_hSocket, pBuffer, nBuffSize, 0);
        if (nRet == SOCKET_ERROR)
        {
            //对方tcp窗口太小暂时发不出去，同时没有超时，则继续等待
            if (::WSAGetLastError() == WSAEWOULDBLOCK && time(NULL) - nStartTime < nTimeout)
            {
                continue;
            } else
                return false;
        } else if (nRet < 1)
        {
            //一旦出现错误就立刻关闭Socket
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
        if (nRet == SOCKET_ERROR)				//一旦出现错误就立刻关闭Socket
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
    //FIXME: 这个函数会被数据发送线程和收取线程同时调用，不安全
    if (m_hSocket == INVALID_SOCKET)
        return;

    ::shutdown(m_hSocket, SD_BOTH);     //同时关闭接受和发送操作，关闭连接
    ::closesocket(m_hSocket);           //关闭套接字
    m_hSocket = INVALID_SOCKET;

    m_bConnected = false;
}
