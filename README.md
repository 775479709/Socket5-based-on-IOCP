# Socket5-based-on-IOCP
一份基于IOCP的高性能socket5服务端，可通过socket5客户端连接实现TCP和UDP代理。
	IOCP事件通知模型
	单线程处理避免互斥锁带来的开销
	全流程异步处理(包括域名解析)
	内存池
	时间队列(检测长时间无动作的连接)

