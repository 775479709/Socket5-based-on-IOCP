#include "IOCP.h"

void CIOCPServer::changeTimer(CIOCPContext *pContext)
{
	if (pContext == NULL)
	{
		return;
	}
	if (pContext->hTimer != NULL)
	{
		ChangeTimerQueueTimer(m_hTimerQueue, pContext->hTimer, timeout * 1000, 2);
	}
}

CIOCPServer::CIOCPServer()
{
	m_nPort = 1080;
	m_nMaxConnections = 20000;
	m_nInitialAccepts = 200;
	MAX_THREAD = 1;
	serverip = 0;
	timeout = 30;
	Onlinecnt = 0;
	ContextQueuecnt = 0;
	m_hListenThread = NULL;
	m_hCompletion = NULL;
	m_lpfnAcceptEx = NULL;
	m_lpfnConnectEx = NULL;
	m_lpfnGetAcceptExSockaddrs = NULL;
	m_bShutDown = FALSE;
	m_hAcceptEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	
	haveBuffercnt = 0;
	haveContextcnt = 0;

	m_hTimerQueue = ::CreateTimerQueue();//时间队列

	WSADATA wsaData;
	WORD sockVersion = MAKEWORD(2, 2);
	::WSAStartup(sockVersion, &wsaData);
}

CIOCPServer::~CIOCPServer()
{
	Shutdown();
	if (m_sListen != INVALID_SOCKET)
	{
		::closesocket(m_sListen);
	}
	if (m_hListenThread != NULL)
	{
		::CloseHandle(m_hListenThread);
	}


	DeleteTimerQueue(m_hTimerQueue);
	::CloseHandle(m_hAcceptEvent);
	::WSACleanup();
	
}

DWORD CIOCPServer::GetInetIP()
{
	// Get host adresses
	char OutIP[256];
	char addr[16];
	struct hostent * pHost;
	pHost = gethostbyname("");
	for (int i = 0; pHost != NULL && pHost->h_addr_list[i] != NULL; i++)
	{
		OutIP[0] = 0;
		for (int j = 0; j < pHost->h_length; j++)
		{
			if (j > 0) strcat(OutIP, ".");
			sprintf(addr, "%u", (unsigned int)((unsigned char*)pHost->h_addr_list[i])[j]);
			strcat(OutIP, addr);
		}
	}
	DWORD IP = inet_addr(OutIP);

	return IP;
}

BOOL CIOCPServer::PostAccept(CIOCPBuffer *pBuffer)  // 在监听套节字上投递Accept请求  
{
	if (pBuffer == NULL)
	{
		printTime();
		printf("PostAccept::pBuffer=%d\n", pBuffer);
		fflush(stdout);
		return false;
	}
	// 设置I/O类型  
	pBuffer->nOperation = OP_ACCEPT;

	// 投递此重叠I/O    
	pBuffer->sClient = ::WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	BOOL b = m_lpfnAcceptEx(m_sListen,
		pBuffer->sClient,
		(PVOID)pBuffer->buff,
		0,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		&pBuffer->dwbytes,
		&pBuffer->ol);
	if (!b && ::WSAGetLastError() != WSA_IO_PENDING)
	{
		printTime();
		printf("poset accept error =%d\n", WSAGetLastError());
		fflush(stdout);
		return FALSE;
	}
	return TRUE;
};

BOOL CIOCPServer::PostRecv(CIOCPContext *pContext, CIOCPBuffer *pBuffer,int step)
{
	
	if (pContext == NULL || pBuffer == NULL || pContext->IsAvailable == true)
	{
		printTime();
		printf("PostRecv::pContext=%d,pBuffer=%d,isavailable=%d\n", pContext, pBuffer, pContext->IsAvailable);
		fflush(stdout);
		return false;
	}
	// 设置I/O类型  
	pBuffer->nOperation = OP_READ;
	// 投递此重叠I/O  

	pBuffer->wsabuf.buf = pBuffer->buff;
	pBuffer->wsabuf.len = pBuffer->nLen;

	
	if (::WSARecv(pContext->sock[pBuffer->op_id], &pBuffer->wsabuf, 1, &pBuffer->dwbytes, &pBuffer->dwflag, &pBuffer->ol, NULL) != NO_ERROR)
	{
		if (::WSAGetLastError() != WSA_IO_PENDING)
		{	
			printTime();
			printf("WSARecv出错：%d\n", WSAGetLastError());
			fflush(stdout);
			return false;
		}
	}
	
	if (step != -1)
	{
		pContext->nCurrentStep = step;
	}
	return true;
}

BOOL CIOCPServer::PostConnect(DWORD ip, WORD port, CIOCPContext *pContext)
{
	if (pContext == NULL || pContext->IsAvailable == true)
	{
		printTime();
		printf("PostConnect::sendto=%d, SendTo->IsAvailable=%d", pContext, pContext->IsAvailable);
		fflush(stdout);
		return false;
	}
	CIOCPBuffer *pBuffer = AllocateBuffer();
	if (pBuffer == NULL)
	{
		printf("connect时缓冲区不足\n");
		return false;
	}
	pBuffer->nOperation = OP_CONNECT;
	pBuffer->sClient = ::WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	pContext->sock[1] = pBuffer->sClient;

	memset(&pContext->addrLocal, 0, sizeof(pContext->addrLocal));
	pContext->addrLocal.sin_family = AF_INET;
	if (bind(pContext->sock[1], (sockaddr *)&pContext->addrLocal, sizeof(sockaddr_in)) == SOCKET_ERROR)
	{
		printTime();
		printf("Post Connect Bind Error!\n");
		fflush(stdout);
		ReleaseBuffer(pBuffer);
		CloseAConnection(pContext);
		return false;
	}
	pBuffer->addr.sin_family = AF_INET;
	pBuffer->addr.sin_addr.s_addr = ip;
	pBuffer->addr.sin_port = port;
	if (CreateIoCompletionPort((HANDLE)pContext->sock[1], m_hCompletion, (DWORD)pContext, 0) != m_hCompletion)
	{
		printTime();
		printf("CONN_ASSOCIATE_SOCKET_FAIL\n");
		fflush(stdout);
		ReleaseBuffer(pBuffer);
		CloseAConnection(pContext);
		return false;
	}
	BOOL bResult = m_lpfnConnectEx(pContext->sock[1],
		(sockaddr *)&pBuffer->addr,  // [in] 对方地址
		sizeof(pBuffer->addr),               // [in] 对方地址长度
		pBuffer->buff,       // [in] 连接后要发送的内容，这里不用
		0,   // [in] 发送内容的字节数 ，这里不用
		&pBuffer->dwbytes,       // [out] 发送了多少个字节，这里不用
		&pBuffer->ol);

	if (!bResult)      // 返回值处理
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)   // 调用失败
		{
			printTime();
			printf("ConnextEx error: %d/n", WSAGetLastError());
			fflush(stdout);
			ReleaseBuffer(pBuffer);
			CloseAConnection(pContext);
			return false;
		}
		else// 操作未决（正在进行中 … ）
		{
#ifdef _DEBUG
			printf("Post ConncetEx OK!\n");// 操作正在进行中
			fflush(stdout);
#endif // _DEBUG	
		}
	}
	pContext->nCurrentStep = 3;

	return true;
}

BOOL CIOCPServer::PostSend(CIOCPContext *pContext, CIOCPBuffer *pBuffer, int step)
{
	if (pContext == NULL || pBuffer == NULL || pContext->IsAvailable == true)//已经被释放掉了
	{
		printTime();
		printf("PostSend::pContext=%d,pBuffer=%d,sock=%d\n", pContext, pBuffer);
		fflush(stdout);
		return false;
	}
	// 设置I/O类型，增加套节字上的重叠I/O计数  
	pBuffer->nOperation = OP_WRITE;

	// 投递此重叠I/O  
	pBuffer->wsabuf.buf = pBuffer->buff;
	pBuffer->wsabuf.len = pBuffer->nLen;

	if (::WSASend(pContext->sock[pBuffer->op_id], &pBuffer->wsabuf, 1, &pBuffer->dwbytes, 0, &pBuffer->ol, NULL) != NO_ERROR)
	{
		int x;
		if ((x = ::WSAGetLastError()) != WSA_IO_PENDING)
		{
			printTime();
			printf("post send error错误码：%d", x);
			fflush(stdout);
			return false;
		}
	}
	
	if (step != -1)
	{
		pContext->nCurrentStep = step;
	}
	
	return true;
}

BOOL CIOCPServer::PostUdpRecv(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	if (pContext == NULL || pBuffer == NULL )
	{
		printTime();
		printf("PostUdpRecv::pContext=%d,pBuffer=%d", pContext, pBuffer);
		fflush(stdout);
		return false;
	}
	pBuffer->nOperation = OP_UDPRECV;
	pBuffer->wsabuf.buf = pBuffer->buff;
	pBuffer->wsabuf.len = pBuffer->nLen;

	if (pContext->IsAvailable == true)
	{
		return false;
	}
	if (WSARecvFrom(pContext->sock[pBuffer->op_id], &pBuffer->wsabuf, 1, &pBuffer->dwbytes, &pBuffer->dwflag,
		(sockaddr*)&pBuffer->addr, &pBuffer->slen, &pBuffer->ol, NULL) != NO_ERROR)
	{

		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
#ifdef _DEBUG
			printf("wsarecvfrom error =%d\n", WSAGetLastError());
#endif // _DEBUG
			printTime();
			printf("wsarecvfrom error =%d\n", WSAGetLastError());
			fflush(stdout);
			return false;
		}

	}
	return true;
}

BOOL CIOCPServer::PostUdpSend(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	if (pContext == NULL || pBuffer == NULL || pContext->IsAvailable == true)
	{
		printTime();
		printf("PosetUdpSend::pContext=%d,pBuffer=%d\n", pContext, pBuffer);
		fflush(stdout);
		return false;
	}
	pBuffer->nOperation = OP_UDPSEND;
	pBuffer->wsabuf.buf = pBuffer->buff;
	pBuffer->wsabuf.len = pBuffer->nLen;
	if (WSASendTo(pContext->sock[pBuffer->op_id], &pBuffer->wsabuf, 1, &pBuffer->dwbytes, 0,
		(sockaddr*)&pBuffer->addr, sizeof(SOCKADDR_IN), &pBuffer->ol, NULL) != NO_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
#ifdef _DEBUG
			printf("wsasendto error =%d\n", WSAGetLastError());
#endif // _DEBUG
			printTime();
			printf("wsasendto error =%d\n", WSAGetLastError());
			fflush(stdout);
			return false;
		}

	}
	return true;
}

bool CIOCPServer::PostDNS(CIOCPContext *pContext, char *domain)
{
	int len = strlen(domain)+1;
	wchar_t  name[100];
	swprintf(name, 100, L"%hs", domain);

	DNSQuery *pDNSQuery = AllocateDNSQuery();
	if (pDNSQuery == NULL)
	{
		return false;
	}
	pContext->dnsq = pDNSQuery;
	pDNSQuery->getvalue((DWORD)this, (DWORD)pContext);
	if (GetAddrInfoExW(name,
		NULL,
		NS_DNS,
		NULL,
		&pDNSQuery->Hint,
		&pDNSQuery->QueryResults,
		&pDNSQuery->timeout,
		&pDNSQuery->QueryOverlapped,
		QueryCompleteCallback,
		NULL) != WSA_IO_PENDING)
	{
		printTime();
		printf("DNS  error=%d\n", WSAGetLastError());
		fflush(stdout);
		ReleaseDNSQuery(pDNSQuery);
		return false;
	}
	return true;
}

BOOL CIOCPServer::Start(int nPort, int nMaxConnections, int  nInitialAccepts,int Timeout)
{
	// 保存用户参数  
	m_nPort = nPort;
	m_nMaxConnections = nMaxConnections;
	m_nInitialAccepts = nInitialAccepts;
	timeout = Timeout;
	serverip= GetInetIP();
	m_bShutDown = false;

	// 创建监听套节字，绑定到本地端口，进入监听模式  
	m_sListen = ::WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN si;
	memset(&si, 0, sizeof(si));
	si.sin_family = AF_INET;
	si.sin_port = ::ntohs(m_nPort);
	si.sin_addr.S_un.S_addr = INADDR_ANY;
	if (::bind(m_sListen, (sockaddr*)&si, sizeof(si)) == SOCKET_ERROR)
	{
		return FALSE;
	}
	::listen(m_sListen, 200);

	// 创建完成端口对象  
	m_hCompletion = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

	// 加载扩展函数AcceptEx  
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	DWORD dwBytes = 0;
	::WSAIoctl(m_sListen,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx,
		sizeof(GuidAcceptEx),
		&m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx),
		&dwBytes,
		NULL,
		NULL);

	// 加载扩展函数GetAcceptExSockaddrs  
	GUID GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	::WSAIoctl(m_sListen,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidGetAcceptExSockaddrs,
		sizeof(GuidGetAcceptExSockaddrs),
		&m_lpfnGetAcceptExSockaddrs,
		sizeof(m_lpfnGetAcceptExSockaddrs),
		&dwBytes,
		NULL,
		NULL
	);

	//加载ConnectEx函数

	GUID GuidConnectEx = WSAID_CONNECTEX;
	if (SOCKET_ERROR == WSAIoctl(m_sListen, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidConnectEx, sizeof(GuidConnectEx),
		&m_lpfnConnectEx, sizeof(m_lpfnConnectEx), &dwBytes, 0, 0))
	{
		printf("WSAIoctl is failed. Error code = %d", WSAGetLastError());
		return 0;
	}

	// 将监听套节字关联到完成端口，注意，这里为它传递的CompletionKey为0  
	::CreateIoCompletionPort((HANDLE)m_sListen, m_hCompletion, (DWORD)0, 0);


	//初始化内存池
	int num = m_nMaxConnections / 10 + 10;
	CIOCPBuffer *pb = new CIOCPBuffer[num * 2];
	haveBuffercnt = num * 2;
	Buffercnt = haveBuffercnt;
	for (int i = 0; i < num * 2; i++)
	{
		BufferQueue.push(&pb[i]);
	}

	CIOCPContext *pc = new CIOCPContext[num];
	haveContextcnt = num;
	ContextQueuecnt = num;
	for (int i = 0; i < num; i++)
	{
		ContextQueue.push(&pc[i]);
		
	}
	DNSQuery *pq = new DNSQuery[num];
	haveDNSQuerycnt = num;
	DNSQuerycnt = num;
	for (int i = 0; i < num; i++)
	{
		DNSQueryQueue.push(&pq[i]);
	}

	// 注册FD_ACCEPT事件。  
	// 如果投递的AcceptEx I/O不够，线程会接收到FD_ACCEPT网络事件，说明应该投递更多的AcceptEx I/O  
	WSAEventSelect(m_sListen, m_hAcceptEvent, FD_ACCEPT);

	// 创建监听线程  
	m_hListenThread = ::CreateThread(NULL, 0, _ListenThreadProc, this, 0, NULL);
	
	return 1;
}

CIOCPBuffer *CIOCPServer::AllocateBuffer()
{
	if (BufferQueue.empty() && haveBuffercnt >= m_nMaxConnections * 2)
	{
		return NULL;
	}
	CIOCPBuffer *pBuffer;
	if (BufferQueue.empty())
	{
		pBuffer = new CIOCPBuffer();
		haveBuffercnt++;
	}
	else
	{
		pBuffer = BufferQueue.front();
		BufferQueue.pop();
		Buffercnt--;
	}

	return pBuffer;
}

DNSQuery *CIOCPServer::AllocateDNSQuery()
{
	if (DNSQueryQueue.empty() && haveDNSQuerycnt >= m_nMaxConnections * 2)
	{
		return NULL;
	}
	DNSQuery *pDNSQuery;
	if (DNSQueryQueue.empty())
	{
		pDNSQuery = new DNSQuery();
		haveDNSQuerycnt++;
		
	}
	else
	{
		pDNSQuery = DNSQueryQueue.front();
		DNSQueryQueue.pop();
		DNSQuerycnt--;
	}

	return pDNSQuery;
}

void CIOCPServer::ReleaseDNSQuery(DNSQuery *pDNSQuery)
{
	if (pDNSQuery == NULL)
	{
		return;
	}
	pDNSQuery->clear();
	fflush(stdout);
	DNSQuerycnt++;
	DNSQueryQueue.push(pDNSQuery);
}

void CIOCPServer::ReleaseBuffer(CIOCPBuffer *pBuffer)
{
	if (pBuffer == NULL)
	{
		return;
	}
	pBuffer->clear();
	Buffercnt++;
	BufferQueue.push(pBuffer);
}

CIOCPContext *CIOCPServer::AllocateContext(SOCKET s)
{
	if (ContextQueue.empty() && haveContextcnt >= m_nMaxConnections)
	{
		return NULL;
	}
	
	CIOCPContext *pContext;
	if (ContextQueue.empty())
	{
		pContext = new CIOCPContext();
		haveContextcnt++;
	}
	else
	{
		pContext = ContextQueue.front();
		ContextQueue.pop();
		ContextQueuecnt--;
	}
	pContext->hCompletion = this->m_hCompletion;
	pContext->sock[0] = s;
	pContext->IsAvailable = false;
	if (!CreateTimerQueueTimer(&pContext->hTimer, m_hTimerQueue, (WAITORTIMERCALLBACK)TimerRoutine, (PVOID)pContext, timeout * 1000, timeout * 1000,0))
	{
		printTime();
		printf("CreateTimerQueueTimer error =%d\n", GetLastError());
		fflush(stdout);
	}

	Online.insert(pContext);
	Onlinecnt++;
	return pContext;
}

void CIOCPServer::CloseAConnection(CIOCPContext *pContext)
{
	if (pContext == NULL)
	{
		return;
	}
	auto it = Online.find(pContext);
	if (it == Online.end())
	{
		return;
	}
	Online.erase(it);
	Onlinecnt--;
	OnConnectionClosing(pContext);          //通知用户
	if (pContext->hTimer != NULL)
	{
		DeleteTimerQueueTimer(m_hTimerQueue, pContext->hTimer, INVALID_HANDLE_VALUE);
		pContext->hTimer = NULL;
	}
	for (int i = 0; i < 4; i++)
	{
		if (pContext->sock[i] != INVALID_SOCKET)
		{
			::closesocket(pContext->sock[i]);
			pContext->sock[i]= INVALID_SOCKET;
		}
	}
	pContext->clear();
	ContextQueue.push(pContext);
	ContextQueuecnt++;
	
}

void CIOCPServer::CloseAllConnections()
{
	std::set<CIOCPContext *>temp(Online);
	for (auto it : temp)
	{
		if (it->IsAvailable == false)
		{
			CloseAConnection(it);
		}
	}
}

DWORD WINAPI CIOCPServer::_ListenThreadProc(LPVOID lpParam)
{
	CIOCPServer *pThis = (CIOCPServer*)lpParam;
	// 先在监听套节字上投递几个Accept I/O  
	CIOCPBuffer *pBuffer;
	for (int i = 0; i<pThis->m_nInitialAccepts; i++)
	{
		pBuffer = pThis->AllocateBuffer();
		if (pBuffer == NULL)
		{
			return -1;
		}
		if (!pThis->PostAccept(pBuffer))
		{
#ifdef _DEBUG
			printf("投递accept失败\n");
#endif // _DEBUG
			return 0;
		}
	}

	HANDLE *hWaitEvents = new HANDLE[1 + pThis->MAX_THREAD];
	int nEventCount = 0;
	hWaitEvents[nEventCount++] = pThis->m_hAcceptEvent;

	// 创建指定数量的工作线程在完成端口上处理I/O
	for (int i = 0; i < pThis->MAX_THREAD; i++)
	{
		hWaitEvents[nEventCount++] = ::CreateThread(NULL, 0, _WorkerThreadProc, pThis, 0, NULL);
		//SetPriorityClass(hWaitEvents[nEventCount - 1], HIGH_PRIORITY_CLASS);//将工作线程的优先级提高
	}
	// 下面进入无限循环，处理事件对象数组中的事件  
	while (TRUE)
	{
		int nIndex = ::WSAWaitForMultipleEvents(nEventCount, hWaitEvents, FALSE, 300 * 1000, FALSE);
		if (nIndex == WSA_WAIT_FAILED || pThis->m_bShutDown == true)
		{
			// 关闭所有连接  
			pThis->CloseAllConnections();
			::Sleep(0);     // 给I/O工作线程一个执行的机会  
		


							// 通知所有I/O处理线程退出  
			for (int i = 0; i <pThis->MAX_THREAD; i++)
			{
				PostQueuedCompletionStatus(pThis->m_hCompletion, 0, (DWORD)NULL, NULL);
			}


			// 等待I/O处理线程退出  
			::WaitForMultipleObjects(pThis->MAX_THREAD, hWaitEvents + 1, TRUE, 5 * 1000);


			for (int i = 1; i<pThis->MAX_THREAD + 1; i++)
			{
				::CloseHandle(hWaitEvents[i]);
			}

			::CloseHandle(pThis->m_hCompletion);

			// 关闭监听套节字  
			::closesocket(pThis->m_sListen);
			pThis->m_sListen = INVALID_SOCKET;

			::ExitThread(0);
		}

		if (nIndex != WSA_WAIT_TIMEOUT)
		{
			nIndex = nIndex - WAIT_OBJECT_0;
			WSANETWORKEVENTS ne;
			int nLimit = 0;
			if (nIndex == 0)         // 2）m_hAcceptEvent事件对象受信，说明投递的Accept请求不够，需要增加  
			{
				::WSAEnumNetworkEvents(pThis->m_sListen, hWaitEvents[nIndex], &ne);
				if (ne.lNetworkEvents & FD_ACCEPT)
				{
					printTime();
					printf("fd_accept 授信\n");
					fflush(stdout);
					nLimit = 50;  // 增加的个数，这里设为50个 
				}
			}
			else if (nIndex >= 1)      // I/O服务线程退出，说明有错误发生，关闭服务器  
			{
				pThis->Shutdown();
				continue;
			}


			// 投递nLimit个AcceptEx I/O请求  
			int i = 0;
			printf("limit=%d\n", nLimit);
			fflush(stdout);
			while (i++ < nLimit )
			{
				pBuffer = pThis->AllocateBuffer();
				printTime();
				printf("正在投递,%d\n",pBuffer);
				fflush(stdout);
				if (pBuffer != NULL)
				{
					if (!pThis->PostAccept(pBuffer))
					{
						printTime();
						printf("ACCEPT授信后投递新的失败\n");
						fflush(stdout);
					}
				}
			}
		}
		else
		{
			int online, free;
			online = pThis->Onlinecnt;
			free = pThis->ContextQueuecnt;
			printTime();
			printf("haveDNSQuerycnt=%d,freeDNSQuerycnt=%d\n", pThis->haveDNSQuerycnt, pThis->DNSQuerycnt);
			printf("haveBuffercnt=%d,freeBuffercnt=%d\n", pThis->haveBuffercnt, pThis->Buffercnt);
			printf("haveContextcnt=%d,online=%d,freecntext=%d,ContextisOK = %s",pThis->haveContextcnt,online,free, pThis->haveContextcnt==online+free?"OK!":"error!");
			fflush(stdout);
			
		}
	}
	delete hWaitEvents;
	return 0;
}

DWORD WINAPI CIOCPServer::_WorkerThreadProc(LPVOID lpParam)
{
#ifdef _DEBUG  
	::printf("   WorkerThread 启动... \n");
#endif // _DEBUG  

	CIOCPServer *pThis = (CIOCPServer*)lpParam;


	CIOCPBuffer *pBuffer = NULL;
	DWORD dwKey;
	DWORD dwTrans;
	LPOVERLAPPED lpol;
	CIOCPContext *pContext = NULL;


	while (TRUE)
	{
		// 在关联到此完成端口的所有套节字上等待I/O完成  
		BOOL bOK = ::GetQueuedCompletionStatus(pThis->m_hCompletion,
			&dwTrans, (PULONG_PTR)&dwKey, (LPOVERLAPPED*)&lpol, WSA_INFINITE);

		if (dwKey != 0)
		{
			pContext = (CIOCPContext *)dwKey;
		}
		else
		{
			pContext = NULL;
		}

		if (dwTrans == -2 && dwKey != NULL && lpol == NULL) //自定义释放资源通知
		{
			if (pContext->dnsq)
			{
				pThis->ReleaseDNSQuery(pContext->dnsq);
			}
			pThis->CloseAConnection(pContext);
			continue;
		}
		//printf("dwtrans=%d,dwKey=%d,lpol=%d,bok=%d\n", dwTrans, dwKey, lpol,bOK);
		if (dwTrans != NULL && dwKey != 0 && lpol == NULL)  //DNS
		{
			pThis->ReleaseDNSQuery(pContext->dnsq);
			pThis->OnDNSQuery(pContext, dwTrans);
			continue;
		}
		if (!bOK)
		{
			if (dwTrans == 0 && dwKey == 0 && lpol == NULL)// iocp句柄在外部被关闭。
			{
				printTime();
				::printf("   主动通知WorkerThread 退出,bok is false!\n");
				fflush(stdout);
				::ExitThread(0);
			}
			else if (dwTrans == 0 && dwKey != NULL && lpol != NULL)//我们主动close一个socket句柄，或者CancelIO(socket)（且此时有未决的操作）或者对端强退（且此时本地有未决的操作）
			{
				pBuffer = CONTAINING_RECORD(lpol, CIOCPBuffer, ol);
				/*if (pBuffer->nOperation == OP_CONNECT)
				{
					int optval, optlen;
					optlen = sizeof(int);
					getsockopt(pBuffer->sClient, SOL_SOCKET, SO_ERROR, (char *)&optval, &optlen);
					if (optval == 0)
					{
						pThis->HandleIO(pContext, pBuffer, dwTrans);
						continue;
					}
				}*/
				pThis->CloseAConnection(pContext);
			}
			else if (dwTrans == -2)
			{
				printTime();
				::printf(" 有特殊事件，dwkey=%d,lpol=%d\n",dwKey,lpol);
				fflush(stdout);
			}
		}
		else
		{
			if (dwTrans == 0)
			{
				if (lpol != NULL)
				{
					
					pBuffer = CONTAINING_RECORD(lpol, CIOCPBuffer, ol);
					if (pBuffer->nOperation == OP_ACCEPT)
					{
						pThis->HandleIO(pContext, pBuffer,dwTrans);
					}
					else if(pBuffer->nOperation == OP_CONNECT)
					{
						pThis->HandleIO(pContext, pBuffer, dwTrans);
					}
					else//客户端优雅的关闭的套接字
					{
						pThis->CloseAConnection(pContext);
						pThis->ReleaseBuffer(pBuffer);
					}
				}
				else //程序主动调用退出
				{
					printTime();
					::printf("   主动通知WorkerThread 退出\n");
					fflush(stdout);
					::ExitThread(0);
				}
			}
			else // 正常处理
			{
				pBuffer = CONTAINING_RECORD(lpol, CIOCPBuffer, ol);
				pThis->HandleIO(pContext, pBuffer, dwTrans);
			}
		}
	}
	return 0;
}


void CIOCPServer::HandleIO(CIOCPContext* pContext, CIOCPBuffer *pBuffer, DWORD dwTrans)
{
	if ( pBuffer == NULL)
	{
		printTime();
		printf("HandIO的参数出现了意外错误，参数地址：%d,%d,%d\n", pContext, pBuffer,dwTrans);
		fflush(stdout);
		return;
	}
	if (pContext != NULL && pContext->IsAvailable == true)//已经标记为关闭
	{
		CloseAConnection(pContext);
		ReleaseBuffer(pBuffer);
		return;
	}
	
	//下面是正式的处理请求
	if (pBuffer->nOperation == OP_ACCEPT)
	{
		// 为新接受的连接申请客户上下文对象  
		CIOCPContext *pClient = AllocateContext(pBuffer->sClient);
		if (pClient == NULL)//缓冲区已满,直接关闭客户套接字
		{
			::closesocket(pBuffer->sClient);
			ReleaseBuffer(pBuffer);
			return;
		}
		// 取得客户地址  
		int nLocalLen, nRmoteLen;
		LPSOCKADDR pLocalAddr, pRemoteAddr;
		m_lpfnGetAcceptExSockaddrs(
			pBuffer->buff,
			0/*sizeof(cmd_header)*/,
			sizeof(sockaddr_in) + 16,
			sizeof(sockaddr_in) + 16,
			(SOCKADDR **)&pLocalAddr,
			&nLocalLen,
			(SOCKADDR **)&pRemoteAddr,
			&nRmoteLen);
		memcpy(&pClient->addrLocal, pLocalAddr, nLocalLen);
		memcpy(&pClient->addrRemote, pRemoteAddr, nRmoteLen);

		// 关联新连接到完成端口对象
		::CreateIoCompletionPort((HANDLE)pBuffer->sClient, m_hCompletion, (DWORD)pClient, 0);
		
		
		// 通知用户  
		pBuffer->nLen = dwTrans;
		OnConnectionEstablished(pClient, pBuffer);
		//投递一个新的接收请求
		
		CIOCPBuffer *p = AllocateBuffer();
		if (p != NULL)
		{
			if (!PostAccept(p))
			{
				printTime();
				printf("ACCEPT后投递新接受失败");
				fflush(stdout);
				ReleaseBuffer(p);
			}
		}
		else
		{
			printf("accept缓存不够\n");
		}
		
		
	}
	else if (pBuffer->nOperation == OP_CONNECT)
	{
		
		OnConnectCompleted(pContext, pBuffer);
		changeTimer(pContext);
	}
	else if (pBuffer->nOperation == OP_READ)
	{
		
		pBuffer->nLen = dwTrans;
		OnReadCompleted(pContext, pBuffer);
		changeTimer(pContext);
	}
	else if (pBuffer->nOperation == OP_WRITE)
	{
		if (dwTrans < pBuffer->nLen)//如果此send没有发送完全，则发送剩下的部分（此部分如果还是没发完全，这里同样进行）  
		{
#ifdef _DEBUG
			printf("send未发送完全，发送：%d，总长度：%d\n", dwTrans, pBuffer->nLen);
#endif // _DEBUG
			
			CIOCPBuffer* p = AllocateBuffer();
			if (p != NULL)
			{
				memcpy(p->buff, pBuffer->buff + dwTrans, pBuffer->nLen - dwTrans);
			}
			if (p == NULL || !PostSend(pContext, p))
			{
				CloseAConnection(pContext);
				ReleaseBuffer(p);
				return;
			}
			ReleaseBuffer(pBuffer);
			changeTimer(pContext);
		}
		else
		{
			
			pBuffer->nLen = dwTrans;
			OnWriteCompleted(pContext, pBuffer);
			changeTimer(pContext);
		}
	}
	else if (pBuffer->nOperation == OP_UDPRECV)
	{
		pBuffer->nLen = dwTrans;
		OnUDPReadCompleted(pContext, pBuffer);
		changeTimer(pContext);
	}
	else if (pBuffer->nOperation == OP_UDPSEND)
	{
		pBuffer->nLen = dwTrans;
		OnUDPWriteCompleted(pContext, pBuffer);
		changeTimer(pContext);
	}
}

void CIOCPServer::Shutdown()
{
	// 通知监听线程，马上停止服务  
	m_bShutDown = TRUE;
	::SetEvent(m_hAcceptEvent);
	::WaitForSingleObject(m_hListenThread, INFINITE);
	::CloseHandle(m_hListenThread);
	m_hListenThread = NULL;
}