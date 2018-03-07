#ifndef __IOCP_H__  
#define __IOCP_H__  


#include <winsock2.h>  
#include <windows.h>  
#include <Mswsock.h> 
#include<cstdio>
#include <ws2tcpip.h>
#include<string>
#include<queue>
#include <comdef.h>
#include<map>
#include<set>
#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "mswsock.lib")


#define BUFFER_SIZE 1024*4    // I/O����Ļ�������С  

struct CIOCPBuffer
{
	CIOCPBuffer()
	{
		slen = sizeof(SOCKADDR_IN);
		clear();
	}

	void clear()
	{
		memset(&ol, 0, sizeof(WSAOVERLAPPED));
		memset(&addr, 0, sizeof(addr));
		sClient = INVALID_SOCKET;
		nLen = BUFFER_SIZE;
		nOperation = 0;
		dwflag = 0;
		dwbytes = 0;
		op_id = 0;
	}

	WSAOVERLAPPED ol;
	SOCKET sClient;						// AcceptEx���յĿͻ����׽���  
	sockaddr_in addr;					//UDPʹ�õĵ�ַ

	char buff[BUFFER_SIZE];             // I/O����ʹ�õĻ�����  
	int nLen;							// buff��������ʹ�õģ���С  
	WSABUF wsabuf;
	int slen;
	DWORD dwflag;
	DWORD dwbytes;
	int op_id;							//����Context��socket id
	int nOperation;						// ��������  
#define OP_ACCEPT   1  
#define OP_WRITE    2  
#define OP_READ     3  
#define OP_CONNECT  4
#define OP_UDPRECV  5
#define OP_UDPSEND  6

};



class DNSQuery;
// ����per-Handle���ݡ���������һ���׽��ֵ���Ϣ  
struct CIOCPContext
{
	CIOCPContext()
	{
		clear();
	}

	void clear()
	{
		memset(sock, -1, sizeof(sock));
		memset(&addrLocal, 0, sizeof(SOCKADDR_IN));
		memset(&addrRemote, 0, sizeof(SOCKADDR_IN));
		nCurrentStep = 0;
		IsAvailable = true;
		isUDP = false;
		hCompletion = NULL;
		hTimer = NULL;
		willconnectport = 0;
		haveconnect = 0;
		dnsq = NULL;
	}

	SOCKET sock[4];
	SOCKADDR_IN addrLocal;          // ���ӵı��ص�ַ  
	SOCKADDR_IN addrRemote;         // ���ӵ�Զ�̵�ַ  
	std::string UserName;			//�û���
	int  nCurrentStep;				//���ڼ�¼��ǰ���ڵĹ��̲�������  
	int haveconnect;				//���ڼ�¼�Ƿ�ͨ��
	bool isUDP;						//�Ƿ���UDP
	WORD willconnectport;			//��Ҫ����Զ�������Ķ˿�
	BOOL IsAvailable;				//�Ƿ��ͷ�
	HANDLE hCompletion;				//�󶨵���ɶ˿�
	HANDLE hTimer;					//��ʱ���
	DNSQuery *dnsq;					

};

class DNSQuery
{
public:
	OVERLAPPED      QueryOverlapped;
	PADDRINFOEX     QueryResults;
	ADDRINFOEX     Hint;
	timeval        timeout;
	DWORD          pThis;
	DWORD          pContext;
	DNSQuery()
	{
		clear();
	}

	void clear()
	{
		memset(this, 0, sizeof(DNSQuery));
		timeout.tv_sec = 5;
	}

	void getvalue(DWORD pthis, DWORD pcontext)
	{
		pThis = pthis;
		pContext = pcontext;
	}

};

class CIOCPServer 
{
public:
	int m_nPort;					// �����������Ķ˿�  
	int m_nMaxConnections;			//�����������
	int  m_nInitialAccepts;			//��ʼ������
	int MAX_THREAD;					//�߳���
	DWORD serverip;					//����ip
	int timeout;					//�ͻ���ʱʱ��
	int m_bShutDown;				//�Ƿ�ر�

	std::queue<CIOCPBuffer *>BufferQueue;
	int Buffercnt;
	int haveBuffercnt;							//�ܹ���buffer����

	std::queue<CIOCPContext *>ContextQueue;
	int ContextQueuecnt;
	int haveContextcnt;							//�ܹ���Context����

	std::queue<DNSQuery *>DNSQueryQueue;
	int DNSQuerycnt;
	int haveDNSQuerycnt;

	std::set<CIOCPContext *>Online;
	int Onlinecnt;

	HANDLE DNS_Completion;						//DNS��ɶ˿�

	HANDLE m_hTimerQueue;						//ʱ�����

	HANDLE m_hAcceptEvent;						//�����¼�				
	HANDLE m_hListenThread;						// �����߳�  
	HANDLE m_hCompletion;						// ��ɶ˿ھ��  
	SOCKET m_sListen;							// �����׽��־��  
	LPFN_ACCEPTEX m_lpfnAcceptEx;				// AcceptEx������ַ  
	LPFN_CONNECTEX m_lpfnConnectEx;				//ConnectEx������ַ
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockaddrs; // GetAcceptExSockaddrs������ַ  

private:    // �̺߳���  
	static DWORD WINAPI _ListenThreadProc(LPVOID lpParam);
	static DWORD WINAPI _WorkerThreadProc(LPVOID lpParam);
public:
	CIOCPServer();
	~CIOCPServer();
	DWORD GetInetIP();

	// ��ʼ����  
	BOOL Start(int nPort = 1080, int nMaxConnections = 20000, int  nInitialAccepts = 200, int Timeout = 10);
	// ֹͣ����  
	void Shutdown();


	// �ر�һ�����Ӻ͹ر���������  
	void CloseAConnection(CIOCPContext *pContext);
	void CloseAllConnections();

	// ȡ�õ�ǰ����������  
	int GetCurrentConnection() { return Online.size(); }

	void changeTimer(CIOCPContext *pContext);

	// ������ͷŻ���������  
	CIOCPBuffer *AllocateBuffer();
	void ReleaseBuffer(CIOCPBuffer *pBuffer);
	DNSQuery *AllocateDNSQuery();


	// ������ͷ��׽���������  
	CIOCPContext *AllocateContext(SOCKET s);

	//Ͷ��һ����������
	BOOL PostAccept(CIOCPBuffer *pBuffer);			
	//Ͷ��һ����������
	BOOL PostSend(CIOCPContext *pContext, CIOCPBuffer *pBuffer,int step = -1);
	//Ͷ��һ����������
	BOOL PostRecv(CIOCPContext *pContext, CIOCPBuffer *pBuffer,int step = -1);
	//Ͷ��һ����������
	BOOL PostConnect(DWORD ip, WORD port, CIOCPContext *pContext);

	bool PostDNS(CIOCPContext *pContext,char *domain);
	void ReleaseDNSQuery(DNSQuery *pDNSQuery);

	BOOL PostUdpRecv(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	BOOL PostUdpSend(CIOCPContext *pContext, CIOCPBuffer *pBuffer);

	void HandleIO(CIOCPContext *pContext, CIOCPBuffer *pBuffer, DWORD dwTrans);


	// �¼�֪ͨ����  
	// ������һ���µ�����  
	virtual void OnConnectionEstablished(CIOCPContext *pContext, CIOCPBuffer *pBuffer) = 0;
	// һ�����ӹر�  
	virtual void OnConnectionClosing(CIOCPContext *pContext) = 0;
	// ��һ�������Ϸ����˴���  
	virtual void OnConnectionError(CIOCPContext *pContext, CIOCPBuffer *pBuffer, int nError) = 0;
	// һ�������ϵĶ��������  
	virtual void OnReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer) = 0;
	// һ�������ϵ�д�������  
	virtual void OnWriteCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer) = 0;
	virtual void OnConnectCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer) = 0;
	virtual void OnUDPReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer) = 0;
	virtual void OnUDPWriteCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer) = 0;
	virtual void OnDNSQuery(CIOCPContext *pContext, DWORD ip) = 0;
};

static void printTime()
{
	SYSTEMTIME st = { 0 };
	GetLocalTime(&st);
	printf("%d-%02d-%02d %02d:%02d:%02d: ",
		st.wYear,
		st.wMonth,
		st.wDay,
		st.wHour,
		st.wMinute,
		st.wSecond);
}


static VOID CALLBACK TimerRoutine(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	CIOCPContext* pContext = (CIOCPContext*)lpParam;
	if (pContext != NULL && pContext->IsAvailable == false)
	{
		if (pContext->hCompletion != NULL)
		{
			PostQueuedCompletionStatus(pContext->hCompletion, -2, (DWORD)pContext, NULL);
		}
	}
}
static
VOID
CALLBACK
QueryCompleteCallback(
	_In_ DWORD Error,
	_In_ DWORD Bytes,
	_In_ LPOVERLAPPED Overlapped
)
{
	DNSQuery	   *QueryContext = NULL;
	PADDRINFOEX     QueryResults = NULL;
	PADDRINFOEX     next = NULL;
	WCHAR           AddrString[64];
	DWORD           AddressStringLength;
	CIOCPServer *pthis;

	//UNREFERENCED_PARAMETER(Bytes);

	QueryContext = CONTAINING_RECORD(Overlapped,DNSQuery, QueryOverlapped);

	if (!QueryContext->pThis)
	{
		goto exit;
	}
	pthis = (CIOCPServer *)QueryContext->pThis;

	if (Error != ERROR_SUCCESS)
	{
		printTime();
		wprintf(L"ResolveName failed with %d\n", Error);
		PostQueuedCompletionStatus(pthis->m_hCompletion, -2, QueryContext->pContext, NULL);
		goto exit;
	}

	//wprintf(L"ResolveName succeeded. Query Results:\n");

	QueryResults = QueryContext->QueryResults;
	if(QueryResults)
	{
		AddressStringLength = 64;

		WSAAddressToString(QueryResults->ai_addr,
			(DWORD)QueryResults->ai_addrlen,
			NULL,
			AddrString,
			&AddressStringLength);
		//wprintf(L"Ip Address: %s\n", AddrString);
		char c[70];
		sprintf(c, "%ws", AddrString);
		DWORD ip = inet_addr(c);
		PostQueuedCompletionStatus(pthis->m_hCompletion, ip, QueryContext->pContext, NULL);
	}

exit:

	if (QueryContext->QueryResults)
	{
		FreeAddrInfoEx(QueryContext->QueryResults);
	}
	return;
}
#endif