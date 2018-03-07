#include "socket5.h"

DWORD DNS(char *buf)//将域名转换为ip字节序
{
	struct hostent *phost = NULL;
	phost = gethostbyname(buf);

	if (phost == NULL)
	{
		printf("Resolve %s error!\n", buf);
		return -1;
	}
	DWORD ip;
	memcpy(&ip, phost->h_addr_list[0], 4);
	//printf("DNS::ip=%u.%u.%u.%u\n", ((unsigned char*)&ip)[0], ((unsigned char*)&ip)[1], ((unsigned char*)&ip)[2], ((unsigned char*)&ip)[3]);
	return ip;
}

bool socket5::ConnectRealServer(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	char *buf = pBuffer->buff;
	if (buf[0] != VERSION || buf[1] == 0x02 || buf[3] == IPV6) //暂不支持bind
	{
		return false;
	}
	if (buf[1] == TCPCONNECT)
	{
		DWORD real_ip;
		WORD real_port;
		if (buf[3] == IPV4)
		{
			memcpy(&real_ip, buf + 4, 4);
			memcpy(&real_port, buf + 8, 2);
		}
		else if (buf[3] == DOMAINNAME)
		{
			byte domain_length = buf[4];
			char target_domain[256] = { 0 };
			memcpy(target_domain, buf + 5, domain_length);
			target_domain[domain_length] = 0;
#ifdef _DEBUG
			printf("target_domain=%s\n", target_domain);
#endif

			memcpy(&real_port, buf + 5 + domain_length, 2);
			pContext->willconnectport = real_port;
			//real_ip = DNS(target_domain);

			return PostDNS(pContext, target_domain);

		}

		return PostConnect(real_ip, real_port, pContext);
	}
	else if (buf[1] == UDPCONNECT)
	{
		pContext->sock[2] = ::WSASocket(AF_INET, SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		pContext->sock[3] = ::WSASocket(AF_INET, SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

		pContext->nCurrentStep = 10;
		pContext->isUDP = true;
		pContext->addrLocal.sin_family = AF_INET;
		pContext->addrLocal.sin_port = 0;
		pContext->addrLocal.sin_addr.s_addr = INADDR_ANY;
		if (bind(pContext->sock[3], (struct sockaddr*) &pContext->addrLocal, sizeof(pContext->addrLocal)) == -1)
		{
			printTime();
			printf("UDP绑定失败\n");
			fflush(stdout);
			return false;
		}
		pContext->addrLocal.sin_family = AF_INET;
		pContext->addrLocal.sin_port = 0;
		pContext->addrLocal.sin_addr.s_addr = INADDR_ANY;
		if (bind(pContext->sock[2], (struct sockaddr*) &pContext->addrLocal, sizeof(pContext->addrLocal)) == -1)
		{
			printTime();
			printf("UDP绑定失败\n");
			fflush(stdout);
			return false;
		}

		int sz = sizeof(sockaddr_in);
		getsockname(pContext->sock[2], (sockaddr*)&pContext->addrLocal, &sz);

		if (::CreateIoCompletionPort((HANDLE)pContext->sock[2], m_hCompletion, (DWORD)pContext, 0)==0)
		{
			printTime();
			printf("绑定到完成端口失败\n");
			fflush(stdout);
			return false;
		}
		if (::CreateIoCompletionPort((HANDLE)pContext->sock[3], m_hCompletion, (DWORD)pContext, 0) == 0)
		{
			printTime();
			printf("绑定到完成端口失败\n");
			fflush(stdout);
			return false;
		}
		
		CIOCPBuffer *pb = AllocateBuffer();
		if (pb == NULL)
		{
			printTime();
			printf("缓冲区已满\n");
			fflush(stdout);
			return false;
		}
		memset(pb->buff, 0, 10);					//flag为10是udp传输
		pb->buff[0] = VERSION;
		pb->buff[3] = 0x01;
		
		memcpy(pb->buff + 4, &serverip, 4);
		memcpy(pb->buff + 8, &pContext->addrLocal.sin_port, 2);
		pContext->addrRemote.sin_family = AF_INET;
		memcpy(&pContext->addrRemote.sin_addr.s_addr, buf + 4, 4);
		memcpy(&pContext->addrRemote.sin_port, buf + 8, 2);

		pBuffer->nLen = 10;

		if (!PostSend(pContext, pb,10))
		{
			ReleaseBuffer(pb);
			return false;
		}
		
		CIOCPBuffer *pb2 = AllocateBuffer();
		if (pb2 == NULL)
		{
			printTime();
			printf("缓冲区已满\n");
			fflush(stdout);
			return false;
		}
		pb2->op_id = 2;
		if (!PostUdpRecv(pContext, pb2))
		{
			ReleaseBuffer(pb2);
			return false;
		}

		CIOCPBuffer *pb3 = AllocateBuffer();
		if (pb3 == NULL)
		{
			printTime();
			printf("缓冲区已满\n");
			fflush(stdout);
			return false;
		}
		pb3->op_id = 3;
		if (!PostUdpRecv(pContext, pb3))
		{
			ReleaseBuffer(pb3);
			return false;
		}
		
		return true;
	}
	return false;
}

bool socket5::AuthPassword(CIOCPBuffer *pBuffer)
{
	if (pBuffer->buff[0] != 0x01)//非协议
	{
		printTime();
		printf("非协议标准\n");
		fflush(stdout);
		return false;
	}
	char recv_name[256];
	char recv_pass[256];
	memcpy(recv_name, pBuffer->buff + 2, pBuffer->buff[1]);
	recv_name[pBuffer->buff[1]] = 0;
	memcpy(recv_pass, pBuffer->buff + 2 + pBuffer->buff[1] + 1, *(pBuffer->buff + 2 + pBuffer->buff[1]));
	recv_pass[*(pBuffer->buff + 2 + pBuffer->buff[1])] = 0;
	return mp[recv_name].Checkpwd(recv_pass);
}

bool socket5::TcpProxy(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	int op_id = pBuffer->op_id;
	int len = pBuffer->nLen;
	pBuffer->clear();
	pBuffer->nLen = len;
	pBuffer->op_id = op_id ^ 1;
	if (!PostSend(pContext, pBuffer))
	{
		ReleaseBuffer(pBuffer);
		CloseAConnection(pContext);
		return false;
	}
	return true;
}

bool socket5::UdpProxy(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	
	if (pBuffer->op_id == 2)
	{
		
		if (pContext->addrRemote.sin_port == 0)
		{
			pContext->addrRemote.sin_port = pBuffer->addr.sin_port;
			pContext->addrRemote.sin_addr.s_addr = pBuffer->addr.sin_addr.s_addr;
		}
		int head_len = 0;
		char *recv_buffer = pBuffer->buff;
		if (recv_buffer[3] == IPV4)//地址为ipv4
		{
			memcpy(&pBuffer->addr.sin_addr.s_addr, recv_buffer + 4, 4);
			memcpy(&pBuffer->addr.sin_port, recv_buffer + 8, 2);
			head_len = 10;
		}
		else if (recv_buffer[3] == DOMAINNAME) //禁用了域名
		{
			byte domain_length = (byte)recv_buffer[4];
			DWORD real_ip;
			char target_domain[256] = { 0 };
			memcpy(target_domain, recv_buffer + 5, domain_length);
			target_domain[domain_length] = 0;
			printTime();
			printf("Udp::target_domain=%s\n", target_domain);
			fflush(stdout);
			if ((real_ip = DNS(target_domain)) == -1)
			{
				printTime();
				printf("DOMAIN Resolve error!\n");
				fflush(stdout);
				ReleaseBuffer(pBuffer);
				return false;
			}
			memcpy(&pBuffer->addr.sin_addr.s_addr, &real_ip, 4);
			memcpy(&pBuffer->addr.sin_port, recv_buffer + 5 + domain_length, 2);
			head_len = 5 + domain_length + 2;
		}
		else
		{
			printTime();
			printf("udp::无法识别地址\n");
			fflush(stdout);
			ReleaseBuffer(pBuffer);
			return false;
		}
		
		memmove(pBuffer->buff, pBuffer->buff + head_len, pBuffer->nLen);
		pBuffer->nLen = pBuffer->nLen - head_len;
		pBuffer->op_id ^= 1;
		memset(&pBuffer->ol, 0, sizeof(pBuffer->ol));
		if (!PostUdpSend(pContext, pBuffer))
		{
			ReleaseBuffer(pBuffer);
			return false;
		}
	}
	else
	{
		if (pContext->addrRemote.sin_port == 0 || pContext->addrRemote.sin_addr.s_addr == 0)
		{
			printTime();
			printf("server first have data!!\n");
			fflush(stdout);
			ReleaseBuffer(pBuffer);
			return false;
		}
		if (pBuffer->nLen > BUFFER_SIZE - 10)
		{
			printTime();
			printf("udpProxy::data have too long!\n");
			fflush(stdout);
			ReleaseBuffer(pBuffer);
			return false;
		}

		for (int i = pBuffer->nLen - 1; i >= 0; i--)
		{
			pBuffer->buff[i + 10] = pBuffer->buff[i];
		}
		memset(pBuffer->buff, 0, 4);
		pBuffer->buff[3] = 0x01;
		memcpy(pBuffer->buff + 4, &pBuffer->addr.sin_addr.s_addr, 4);
		memcpy(pBuffer->buff + 8, &pBuffer->addr.sin_port, 2);
		pBuffer->nLen = pBuffer->nLen + 10;
		pBuffer->op_id ^= 1;
		pBuffer->addr = pContext->addrRemote;
		memset(&pBuffer->ol, 0, sizeof(pBuffer->ol));
		if (!PostUdpSend(pContext, pBuffer))
		{
			ReleaseBuffer(pBuffer);
			return false;
		}
	}
	return true;
}

void socket5::OnUDPReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	if (!UdpProxy(pContext, pBuffer))
	{
		CloseAConnection(pContext);
	}
	
}

void socket5::OnUDPWriteCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer) 
{
	int op_id = pBuffer->op_id;
	pBuffer->clear();
	pBuffer->op_id = op_id ^ 1;
	if (!PostUdpRecv(pContext, pBuffer))
	{
		ReleaseBuffer(pBuffer);
		CloseAConnection(pContext);
	}
}

void socket5::OnDNSQuery(CIOCPContext *pContext, DWORD ip)
{
	if (!PostConnect(ip, pContext->willconnectport, pContext))
	{
		CloseAConnection(pContext);
	}
}

void socket5::OnConnectionEstablished(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
#ifdef _DEBUG
	printf("接收到一个新的连接(%d): %s\n", GetCurrentConnection(), ::inet_ntoa(pContext->addrRemote.sin_addr));
	printf("接受到一个数据包, 其大小为: %d字节\n", pBuffer->nLen);
#endif
	pBuffer->clear();
	if (!PostRecv(pContext, pBuffer))
	{
		ReleaseBuffer(pBuffer);
		CloseAConnection(pContext);
	}
}

void socket5::OnConnectionClosing(CIOCPContext *pContext)
{
#ifdef _DEBUG
	printf("一个连接关闭\n");
#endif
}

void socket5::OnConnectionError(CIOCPContext *pContext, CIOCPBuffer *pBuffer, int nError)
{
#ifdef _DEBUG
	printf("一个连接发生错误: %d\n", nError);
#endif
}

void socket5::OnReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
#ifdef _DEBUG
	printf("接受到一个数据包, 其大小为: %d字节,当前执行位置：%d\n", pBuffer->nLen, pContext->nCurrentStep);
#endif
	if (pContext->nCurrentStep == 0)
	{
		if (pBuffer->buff[0] != VERSION)
		{
			CloseAConnection(pContext);
			ReleaseBuffer(pBuffer);
			return;
		}
		CIOCPBuffer *p = AllocateBuffer();
		if (p != NULL)
		{
			p->buff[0] = 0x05, p->buff[1] = 0x02;
			p->nLen = 2;
			if (!PostSend(pContext, p, 1))
			{
				ReleaseBuffer(p);
				CloseAConnection(pContext);
				ReleaseBuffer(pBuffer);
				return;
			}
		}
		pBuffer->clear();
		if (!PostRecv(pContext, pBuffer))
		{
			ReleaseBuffer(pBuffer);
			CloseAConnection(pContext);
			return;
		}

	}
	else if (pContext->nCurrentStep == 1)
	{
		bool ret = AuthPassword(pBuffer);
#ifdef _DEBUG
		if (ret)
		{
			printf("账号验证成功\n");
		}
		else
		{
			printf("用户密码错误\n");
		}
#endif
		pBuffer->clear();
		pBuffer->buff[0] = 0x01;
		pBuffer->buff[1] = !ret;
		pBuffer->nLen = 2;
		if (!ret || !PostSend(pContext, pBuffer, 2))
		{
			ReleaseBuffer(pBuffer);
			CloseAConnection(pContext);
		}
		else
		{
			CIOCPBuffer *ps = AllocateBuffer();
			if (ps == NULL)
			{
				printf("内存池申请失败！\n");
			}
			else if (!PostRecv(pContext, ps))
			{
				ReleaseBuffer(ps);
				CloseAConnection(pContext);
			}
		}

	}
	else if (pContext->nCurrentStep == 2)
	{
		if (!ConnectRealServer(pContext, pBuffer))
		{
			CloseAConnection(pContext);
		}
		ReleaseBuffer(pBuffer);
	}
	else if (pContext->nCurrentStep == 3)
	{
		if (!TcpProxy(pContext, pBuffer))
		{
			CloseAConnection(pContext);
		}
	}
	else if (pContext->nCurrentStep == 10)
	{
		pBuffer->clear();
		if (!PostRecv(pContext, pBuffer))
		{
			ReleaseBuffer(pBuffer);
			CloseAConnection(pContext);
		}
	}
	else
	{
		ReleaseBuffer(pBuffer);
	}
}

void socket5::OnWriteCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	if (pContext->nCurrentStep == 3)
	{
		int op_id = pBuffer->op_id;
		pBuffer->clear();
		pBuffer->op_id = op_id ^ 1;
		if (!PostRecv(pContext, pBuffer))
		{
			ReleaseBuffer(pBuffer);
			CloseAConnection(pContext);
		}
	}
	else
	{
		ReleaseBuffer(pBuffer);
	}

}

void socket5::OnConnectCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	CIOCPBuffer *p = AllocateBuffer();
	CIOCPBuffer *ps = AllocateBuffer();
	if (p == NULL || ps == NULL)
	{
		printf("内存池申请失败！\n");
		return;
	}
#ifdef _DEBUG
	printf("tcp连接成功\n");
#endif
	memset(p->buff, 0, 10);
	p->buff[0] = 0x05;
	p->buff[3] = 0x01;
	p->nLen = 10;
	if (!PostSend(pContext, p, 3))
	{
		ReleaseBuffer(p);
		CloseAConnection(pContext);
	}
	if (!PostRecv(pContext, ps, 3))
	{
		ReleaseBuffer(ps);
		CloseAConnection(pContext);
	}
	ReleaseBuffer(pBuffer);
}

