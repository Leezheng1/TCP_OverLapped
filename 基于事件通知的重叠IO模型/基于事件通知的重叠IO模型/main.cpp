#include <stdio.h>
#include "OverlapIOModel.h"

enum{PORT = 4567};
OverlapIOModel* g_overLap = NULL;

void ServerNetCallBack(DWORD id, void* param, int len)
{
	char* buf = (char*)param;
	printf("%s\n", buf);
	g_overLap->WSASendToClient(id, buf);
}

int main()
{
 	g_overLap = new OverlapIOModel;
 	g_overLap->InitNet(ServerNetCallBack, PORT, "127.0.0.1");

	return 0;
}