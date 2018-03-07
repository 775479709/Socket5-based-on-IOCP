#include "IOCP.h"
#include<map>
#define VERSION 0x05
#define TCPCONNECT 0x01
#define UDPCONNECT 0x03
#define IPV4 0x01
#define DOMAINNAME 0x03
#define IPV6 0x04
#define AUTH_CODE 0x02

class ClientInfo
{
public:
	ClientInfo(char *name = NULL, char *pwd = NULL, long long f = 0, int mxcon = 200)
	{
		if (name != NULL)
		{
			strcpy(UserName, name);
		}
		if (pwd != NULL)
		{
			strcpy(PassWord, pwd);
		}
		flow = f;		//流量
		Maxcon = mxcon;//最大链接数
	}
	char UserName[256];
	char PassWord[256];
	long long flow;
	int Maxcon;
	bool Checkpwd(char *pwd)
	{
		if (strcmp(pwd, PassWord) == 0 && flow > 0)
		{
			return true;
		}
		return false;
	}
};



class socket5 :public CIOCPServer
{
public:
	std::map<std::string, ClientInfo>mp;
	bool AuthPassword(CIOCPBuffer *pBuffer);

	bool ConnectRealServer(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	bool TcpProxy(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	bool UdpProxy(CIOCPContext *pContext, CIOCPBuffer *pBuffer);

	void OnConnectionEstablished(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	void OnConnectionClosing(CIOCPContext *pContext);
	void OnConnectionError(CIOCPContext *pContext, CIOCPBuffer *pBuffer, int nError);
	void OnReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	void OnWriteCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	void OnConnectCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	void OnUDPReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	void OnUDPWriteCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	void OnDNSQuery(CIOCPContext *pContext, DWORD ip);
};