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


#define BUFFER_SIZE 1024*4    // I/O请求的缓冲区大小  

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
	SOCKET sClient;						// AcceptEx接收的客户方套节字  
	sockaddr_in addr;					//UDP使用的地址

	char buff[BUFFER_SIZE];             // I/O操作使用的缓冲区  
	int nLen;							// buff缓冲区（使用的）大小  
	WSABUF wsabuf;
	int slen;
	DWORD dwflag;
	DWORD dwbytes;
	int op_id;							//操作Context的socket id
	int nOperation;						// 操作类型  
#define OP_ACCEPT   1  
#define OP_WRITE    2  
#define OP_READ     3  
#define OP_CONNECT  4
#define OP_UDPRECV  5
#define OP_UDPSEND  6

};



class DNSQuery;
// 这是per-Handle数据。它包含了一个套节字的信息  
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
	SOCKADDR_IN addrLocal;          // 连接的本地地址  
	SOCKADDR_IN addrRemote;         // 连接的远程地址  
	std::string UserName;			//用户名
	int  nCurrentStep;				//用于记录当前处于的过程步骤数。  
	int haveconnect;				//用于记录是否通信
	bool isUDP;						//是否是UDP
	WORD willconnectport;			//将要连接远程主机的端口
	BOOL IsAvailable;				//是否被释放
	HANDLE hCompletion;				//绑定的完成端口
	HANDLE hTimer;					//超时检测
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
	int m_nPort;					// 服务器监听的端口  
	int m_nMaxConnections;			//最大连接数量
	int  m_nInitialAccepts;			//起始连接数
	int MAX_THREAD;					//线程数
	DWORD serverip;					//外网ip
	int timeout;					//客户超时时间
	int m_bShutDown;				//是否关闭

	std::queue<CIOCPBuffer *>BufferQueue;
	int Buffercnt;
	int haveBuffercnt;							//总共的buffer数量

	std::queue<CIOCPContext *>ContextQueue;
	int ContextQueuecnt;
	int haveContextcnt;							//总共的Context数量

	std::queue<DNSQuery *>DNSQueryQueue;
	int DNSQuerycnt;
	int haveDNSQuerycnt;

	std::set<CIOCPContext *>Online;
	int Onlinecnt;

	HANDLE DNS_Completion;						//DNS完成端口

	HANDLE m_hTimerQueue;						//时间队列

	HANDLE m_hAcceptEvent;						//接受事件				
	HANDLE m_hListenThread;						// 监听线程  
	HANDLE m_hCompletion;						// 完成端口句柄  
	SOCKET m_sListen;							// 监听套节字句柄  
	LPFN_ACCEPTEX m_lpfnAcceptEx;				// AcceptEx函数地址  
	LPFN_CONNECTEX m_lpfnConnectEx;				//ConnectEx函数地址
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockaddrs; // GetAcceptExSockaddrs函数地址  

private:    // 线程函数  
	static DWORD WINAPI _ListenThreadProc(LPVOID lpParam);
	static DWORD WINAPI _WorkerThreadProc(LPVOID lpParam);
public:
	CIOCPServer();
	~CIOCPServer();
	DWORD GetInetIP();

	// 开始服务  
	BOOL Start(int nPort = 1080, int nMaxConnections = 20000, int  nInitialAccepts = 200, int Timeout = 10);
	// 停止服务  
	void Shutdown();


	// 关闭一个连接和关闭所有连接  
	void CloseAConnection(CIOCPContext *pContext);
	void CloseAllConnections();

	// 取得当前的连接数量  
	int GetCurrentConnection() { return Online.size(); }

	void changeTimer(CIOCPContext *pContext);

	// 申请和释放缓冲区对象  
	CIOCPBuffer *AllocateBuffer();
	void ReleaseBuffer(CIOCPBuffer *pBuffer);
	DNSQuery *AllocateDNSQuery();


	// 申请和释放套节字上下文  
	CIOCPContext *AllocateContext(SOCKET s);

	//投递一个接受请求
	BOOL PostAccept(CIOCPBuffer *pBuffer);			
	//投递一个发送请求
	BOOL PostSend(CIOCPContext *pContext, CIOCPBuffer *pBuffer,int step = -1);
	//投递一个接收请求
	BOOL PostRecv(CIOCPContext *pContext, CIOCPBuffer *pBuffer,int step = -1);
	//投递一个连接请求
	BOOL PostConnect(DWORD ip, WORD port, CIOCPContext *pContext);

	bool PostDNS(CIOCPContext *pContext,char *domain);
	void ReleaseDNSQuery(DNSQuery *pDNSQuery);

	BOOL PostUdpRecv(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	BOOL PostUdpSend(CIOCPContext *pContext, CIOCPBuffer *pBuffer);

	void HandleIO(CIOCPContext *pContext, CIOCPBuffer *pBuffer, DWORD dwTrans);


	// 事件通知函数  
	// 建立了一个新的连接  
	virtual void OnConnectionEstablished(CIOCPContext *pContext, CIOCPBuffer *pBuffer) = 0;
	// 一个连接关闭  
	virtual void OnConnectionClosing(CIOCPContext *pContext) = 0;
	// 在一个连接上发生了错误  
	virtual void OnConnectionError(CIOCPContext *pContext, CIOCPBuffer *pBuffer, int nError) = 0;
	// 一个连接上的读操作完成  
	virtual void OnReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer) = 0;
	// 一个连接上的写操作完成  
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