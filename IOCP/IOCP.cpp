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

	m_hTimerQueue = ::CreateTimerQueue();//ʱ�����

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

BOOL CIOCPServer::PostAccept(CIOCPBuffer *pBuffer)  // �ڼ����׽�����Ͷ��Accept����  
{
	if (pBuffer == NULL)
	{
		printTime();
		printf("PostAccept::pBuffer=%d\n", pBuffer);
		fflush(stdout);
		return false;
	}
	// ����I/O����  
	pBuffer->nOperation = OP_ACCEPT;

	// Ͷ�ݴ��ص�I/O    
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
	// ����I/O����  
	pBuffer->nOperation = OP_READ;
	// Ͷ�ݴ��ص�I/O  

	pBuffer->wsabuf.buf = pBuffer->buff;
	pBuffer->wsabuf.len = pBuffer->nLen;

	
	if (::WSARecv(pContext->sock[pBuffer->op_id], &pBuffer->wsabuf, 1, &pBuffer->dwbytes, &pBuffer->dwflag, &pBuffer->ol, NULL) != NO_ERROR)
	{
		if (::WSAGetLastError() != WSA_IO_PENDING)
		{	
			printTime();
			printf("WSARecv����%d\n", WSAGetLastError());
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
		printf("connectʱ����������\n");
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
		(sockaddr *)&pBuffer->addr,  // [in] �Է���ַ
		sizeof(pBuffer->addr),               // [in] �Է���ַ����
		pBuffer->buff,       // [in] ���Ӻ�Ҫ���͵����ݣ����ﲻ��
		0,   // [in] �������ݵ��ֽ��� �����ﲻ��
		&pBuffer->dwbytes,       // [out] �����˶��ٸ��ֽڣ����ﲻ��
		&pBuffer->ol);

	if (!bResult)      // ����ֵ����
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)   // ����ʧ��
		{
			printTime();
			printf("ConnextEx error: %d/n", WSAGetLastError());
			fflush(stdout);
			ReleaseBuffer(pBuffer);
			CloseAConnection(pContext);
			return false;
		}
		else// ����δ�������ڽ����� �� ��
		{
#ifdef _DEBUG
			printf("Post ConncetEx OK!\n");// �������ڽ�����
			fflush(stdout);
#endif // _DEBUG	
		}
	}
	pContext->nCurrentStep = 3;

	return true;
}

BOOL CIOCPServer::PostSend(CIOCPContext *pContext, CIOCPBuffer *pBuffer, int step)
{
	if (pContext == NULL || pBuffer == NULL || pContext->IsAvailable == true)//�Ѿ����ͷŵ���
	{
		printTime();
		printf("PostSend::pContext=%d,pBuffer=%d,sock=%d\n", pContext, pBuffer);
		fflush(stdout);
		return false;
	}
	// ����I/O���ͣ������׽����ϵ��ص�I/O����  
	pBuffer->nOperation = OP_WRITE;

	// Ͷ�ݴ��ص�I/O  
	pBuffer->wsabuf.buf = pBuffer->buff;
	pBuffer->wsabuf.len = pBuffer->nLen;

	if (::WSASend(pContext->sock[pBuffer->op_id], &pBuffer->wsabuf, 1, &pBuffer->dwbytes, 0, &pBuffer->ol, NULL) != NO_ERROR)
	{
		int x;
		if ((x = ::WSAGetLastError()) != WSA_IO_PENDING)
		{
			printTime();
			printf("post send error�����룺%d", x);
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
	// �����û�����  
	m_nPort = nPort;
	m_nMaxConnections = nMaxConnections;
	m_nInitialAccepts = nInitialAccepts;
	timeout = Timeout;
	serverip= GetInetIP();
	m_bShutDown = false;

	// ���������׽��֣��󶨵����ض˿ڣ��������ģʽ  
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

	// ������ɶ˿ڶ���  
	m_hCompletion = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

	// ������չ����AcceptEx  
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

	// ������չ����GetAcceptExSockaddrs  
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

	//����ConnectEx����

	GUID GuidConnectEx = WSAID_CONNECTEX;
	if (SOCKET_ERROR == WSAIoctl(m_sListen, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidConnectEx, sizeof(GuidConnectEx),
		&m_lpfnConnectEx, sizeof(m_lpfnConnectEx), &dwBytes, 0, 0))
	{
		printf("WSAIoctl is failed. Error code = %d", WSAGetLastError());
		return 0;
	}

	// �������׽��ֹ�������ɶ˿ڣ�ע�⣬����Ϊ�����ݵ�CompletionKeyΪ0  
	::CreateIoCompletionPort((HANDLE)m_sListen, m_hCompletion, (DWORD)0, 0);


	//��ʼ���ڴ��
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

	// ע��FD_ACCEPT�¼���  
	// ���Ͷ�ݵ�AcceptEx I/O�������̻߳���յ�FD_ACCEPT�����¼���˵��Ӧ��Ͷ�ݸ����AcceptEx I/O  
	WSAEventSelect(m_sListen, m_hAcceptEvent, FD_ACCEPT);

	// ���������߳�  
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
	OnConnectionClosing(pContext);          //֪ͨ�û�
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
	// ���ڼ����׽�����Ͷ�ݼ���Accept I/O  
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
			printf("Ͷ��acceptʧ��\n");
#endif // _DEBUG
			return 0;
		}
	}

	HANDLE *hWaitEvents = new HANDLE[1 + pThis->MAX_THREAD];
	int nEventCount = 0;
	hWaitEvents[nEventCount++] = pThis->m_hAcceptEvent;

	// ����ָ�������Ĺ����߳�����ɶ˿��ϴ���I/O
	for (int i = 0; i < pThis->MAX_THREAD; i++)
	{
		hWaitEvents[nEventCount++] = ::CreateThread(NULL, 0, _WorkerThreadProc, pThis, 0, NULL);
		//SetPriorityClass(hWaitEvents[nEventCount - 1], HIGH_PRIORITY_CLASS);//�������̵߳����ȼ����
	}
	// �����������ѭ���������¼����������е��¼�  
	while (TRUE)
	{
		int nIndex = ::WSAWaitForMultipleEvents(nEventCount, hWaitEvents, FALSE, 300 * 1000, FALSE);
		if (nIndex == WSA_WAIT_FAILED || pThis->m_bShutDown == true)
		{
			// �ر���������  
			pThis->CloseAllConnections();
			::Sleep(0);     // ��I/O�����߳�һ��ִ�еĻ���  
		


							// ֪ͨ����I/O�����߳��˳�  
			for (int i = 0; i <pThis->MAX_THREAD; i++)
			{
				PostQueuedCompletionStatus(pThis->m_hCompletion, 0, (DWORD)NULL, NULL);
			}


			// �ȴ�I/O�����߳��˳�  
			::WaitForMultipleObjects(pThis->MAX_THREAD, hWaitEvents + 1, TRUE, 5 * 1000);


			for (int i = 1; i<pThis->MAX_THREAD + 1; i++)
			{
				::CloseHandle(hWaitEvents[i]);
			}

			::CloseHandle(pThis->m_hCompletion);

			// �رռ����׽���  
			::closesocket(pThis->m_sListen);
			pThis->m_sListen = INVALID_SOCKET;

			::ExitThread(0);
		}

		if (nIndex != WSA_WAIT_TIMEOUT)
		{
			nIndex = nIndex - WAIT_OBJECT_0;
			WSANETWORKEVENTS ne;
			int nLimit = 0;
			if (nIndex == 0)         // 2��m_hAcceptEvent�¼��������ţ�˵��Ͷ�ݵ�Accept���󲻹�����Ҫ����  
			{
				::WSAEnumNetworkEvents(pThis->m_sListen, hWaitEvents[nIndex], &ne);
				if (ne.lNetworkEvents & FD_ACCEPT)
				{
					printTime();
					printf("fd_accept ����\n");
					fflush(stdout);
					nLimit = 50;  // ���ӵĸ�����������Ϊ50�� 
				}
			}
			else if (nIndex >= 1)      // I/O�����߳��˳���˵���д��������رշ�����  
			{
				pThis->Shutdown();
				continue;
			}


			// Ͷ��nLimit��AcceptEx I/O����  
			int i = 0;
			printf("limit=%d\n", nLimit);
			fflush(stdout);
			while (i++ < nLimit )
			{
				pBuffer = pThis->AllocateBuffer();
				printTime();
				printf("����Ͷ��,%d\n",pBuffer);
				fflush(stdout);
				if (pBuffer != NULL)
				{
					if (!pThis->PostAccept(pBuffer))
					{
						printTime();
						printf("ACCEPT���ź�Ͷ���µ�ʧ��\n");
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
	::printf("   WorkerThread ����... \n");
#endif // _DEBUG  

	CIOCPServer *pThis = (CIOCPServer*)lpParam;


	CIOCPBuffer *pBuffer = NULL;
	DWORD dwKey;
	DWORD dwTrans;
	LPOVERLAPPED lpol;
	CIOCPContext *pContext = NULL;


	while (TRUE)
	{
		// �ڹ���������ɶ˿ڵ������׽����ϵȴ�I/O���  
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

		if (dwTrans == -2 && dwKey != NULL && lpol == NULL) //�Զ����ͷ���Դ֪ͨ
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
			if (dwTrans == 0 && dwKey == 0 && lpol == NULL)// iocp������ⲿ���رա�
			{
				printTime();
				::printf("   ����֪ͨWorkerThread �˳�,bok is false!\n");
				fflush(stdout);
				::ExitThread(0);
			}
			else if (dwTrans == 0 && dwKey != NULL && lpol != NULL)//��������closeһ��socket���������CancelIO(socket)���Ҵ�ʱ��δ���Ĳ��������߶Զ�ǿ�ˣ��Ҵ�ʱ������δ���Ĳ�����
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
				::printf(" �������¼���dwkey=%d,lpol=%d\n",dwKey,lpol);
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
					else//�ͻ������ŵĹرյ��׽���
					{
						pThis->CloseAConnection(pContext);
						pThis->ReleaseBuffer(pBuffer);
					}
				}
				else //�������������˳�
				{
					printTime();
					::printf("   ����֪ͨWorkerThread �˳�\n");
					fflush(stdout);
					::ExitThread(0);
				}
			}
			else // ��������
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
		printf("HandIO�Ĳ���������������󣬲�����ַ��%d,%d,%d\n", pContext, pBuffer,dwTrans);
		fflush(stdout);
		return;
	}
	if (pContext != NULL && pContext->IsAvailable == true)//�Ѿ����Ϊ�ر�
	{
		CloseAConnection(pContext);
		ReleaseBuffer(pBuffer);
		return;
	}
	
	//��������ʽ�Ĵ�������
	if (pBuffer->nOperation == OP_ACCEPT)
	{
		// Ϊ�½��ܵ���������ͻ������Ķ���  
		CIOCPContext *pClient = AllocateContext(pBuffer->sClient);
		if (pClient == NULL)//����������,ֱ�ӹرտͻ��׽���
		{
			::closesocket(pBuffer->sClient);
			ReleaseBuffer(pBuffer);
			return;
		}
		// ȡ�ÿͻ���ַ  
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

		// ���������ӵ���ɶ˿ڶ���
		::CreateIoCompletionPort((HANDLE)pBuffer->sClient, m_hCompletion, (DWORD)pClient, 0);
		
		
		// ֪ͨ�û�  
		pBuffer->nLen = dwTrans;
		OnConnectionEstablished(pClient, pBuffer);
		//Ͷ��һ���µĽ�������
		
		CIOCPBuffer *p = AllocateBuffer();
		if (p != NULL)
		{
			if (!PostAccept(p))
			{
				printTime();
				printf("ACCEPT��Ͷ���½���ʧ��");
				fflush(stdout);
				ReleaseBuffer(p);
			}
		}
		else
		{
			printf("accept���治��\n");
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
		if (dwTrans < pBuffer->nLen)//�����sendû�з�����ȫ������ʣ�µĲ��֣��˲����������û����ȫ������ͬ�����У�  
		{
#ifdef _DEBUG
			printf("sendδ������ȫ�����ͣ�%d���ܳ��ȣ�%d\n", dwTrans, pBuffer->nLen);
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
	// ֪ͨ�����̣߳�����ֹͣ����  
	m_bShutDown = TRUE;
	::SetEvent(m_hAcceptEvent);
	::WaitForSingleObject(m_hListenThread, INFINITE);
	::CloseHandle(m_hListenThread);
	m_hListenThread = NULL;
}