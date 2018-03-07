#include "socket5.h"

int main()
{
	freopen("log.txt", "a+", stdout);
	socket5 *server = new socket5();
	server->mp["bin"] = ClientInfo("bin", "bin", 1ll << 60);
	server->Start(1080,20000,20,300);

	// �����¼�������ServerShutdown�����ܹ��ر��Լ�  
	HANDLE hEvent = ::CreateEvent(NULL, FALSE, FALSE, (LPCTSTR)"ShutdownEvent");
	::WaitForSingleObject(hEvent, INFINITE);
	::CloseHandle(hEvent);

	// �رշ���  
	server->Shutdown();
	delete server;
	return 0;
}