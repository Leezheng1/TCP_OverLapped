#pragma once
#include "Socket.h"

#define MSGSIZE 1024

/************************************************************************/
/* ע�⣬�˴��� WSAOVERLAPPED ��һ��Ҫ���ײ������ֺ�������̵���������������н��ͣ� 
/************************************************************************/
typedef struct
{
	WSAOVERLAPPED overlap;		// ���ﱣ���һ���¼�����...��������λ��
	WSABUF wsaBuf;				// ָ���������ĳ�Ա 
	char szMessage[MSGSIZE];	// �����Ļ�����
	DWORD NumberOfBytesRecvd;	// ���յ����ֽ�
	DWORD Flags;				// �Ƿ�ɹ�����
	SOCKADDR_IN addr;			// �ͻ�����Ϣ
	SOCKET sock;				// �׽���
}PER_IO_OPERATION_DATA, *LPPER_IO_OPERATION_DATA;


typedef void(*NetCallBack) (DWORD id, void* param, int len); // ������Ϣ������ָ��


/************************************************************************/
/* �����¼�֪ͨ���ص�IOģ���� 
/* SocketΪ�Լ���װ��socket��
/************************************************************************/
class OverlapIOModel : public Socket
{
private:
	int g_iTotalConn;									// �ܵ�������
	WSAEVENT g_CliEventArr[MAXIMUM_WAIT_OBJECTS];		// �¼���������
	LPPER_IO_OPERATION_DATA g_pPerIODataArr[MAXIMUM_WAIT_OBJECTS];	// �ص��ṹ����

protected:
	// ��Ϣ������
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

