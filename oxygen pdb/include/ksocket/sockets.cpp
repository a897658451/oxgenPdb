#include "sockets.h"

#pragma warning(disable :4996)
//����������з�װ
namespace helper {
	PUNICODE_STRING g_pRegistryPath = NULL;
	PWORK_QUEUE_ITEM g_pUnloadWorkItem = NULL;
	//��ж��
	void unloadSelf() {
		if (g_pUnloadWorkItem == nullptr || g_pRegistryPath == nullptr) return;
		ExQueueWorkItem(g_pUnloadWorkItem, DelayedWorkQueue);
	}
	void unloadinit(PUNICODE_STRING reg_path) {

		g_pRegistryPath = (PUNICODE_STRING)ExAllocatePool(NonPagedPool, sizeof(UNICODE_STRING));
		g_pRegistryPath->Buffer = (PWCH)ExAllocatePool(NonPagedPool, reg_path->MaximumLength);
		if (!g_pRegistryPath->Buffer || !g_pUnloadWorkItem) return;
		

		g_pRegistryPath->Length = reg_path->Length;
		g_pRegistryPath->MaximumLength = reg_path->MaximumLength;
		memcpy(g_pRegistryPath->Buffer, reg_path->Buffer, g_pRegistryPath->Length);

		g_pUnloadWorkItem = (PWORK_QUEUE_ITEM)ExAllocatePool(NonPagedPool, sizeof(WORK_QUEUE_ITEM));
		ExInitializeWorkItem(g_pUnloadWorkItem, (PWORKER_THREAD_ROUTINE)ZwUnloadDriver, g_pRegistryPath);
	}
	using threadFunc = void (*)(void*);

	class _thread {
	public:
		_thread(threadFunc func,void* context=nullptr) :_func(func) {

			_status = PsCreateSystemThread(
				&_hThread,
				GENERIC_ALL,
				nullptr,
				nullptr,
				nullptr,
				_func,
				context);

		}

		NTSTATUS getStatus() const{

			return _status;
		}
		HANDLE getHandle()const {

			return _hThread;
		}
	private:
		threadFunc _func;
		HANDLE _hThread;
		NTSTATUS _status;
	};
	bool writeToDisk(const wchar_t* path, const wchar_t* file_name, char* buf, size_t buf_len)
	{
		
		NTSTATUS status=STATUS_UNSUCCESSFUL;
		OBJECT_ATTRIBUTES oa{ 0 };
		IO_STATUS_BLOCK    ioStatusBlock{0};
		HANDLE hFile = 0;
		UNICODE_STRING ufilePath{ 0 };

		wchar_t filePath[266] = { 0 };

		wcscpy(filePath, L"\\??\\");
		wcscat(filePath, path);
		wcscat(filePath,file_name);
		RtlInitUnicodeString(&ufilePath, filePath);
		InitializeObjectAttributes(&oa, &ufilePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

		status = ZwCreateFile(&hFile, GENERIC_WRITE, &oa, &ioStatusBlock, 0, FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
		if (!NT_SUCCESS(status)) {
			DbgPrint("Failed to create or open the file: 0x%X\n", status);
			return false;;
		}

		status = ZwWriteFile(hFile, NULL, NULL, NULL, &ioStatusBlock, buf, (ULONG)buf_len, NULL, NULL);
		if (!NT_SUCCESS(status)) {
			DbgPrint("Failed to write to the file: 0x%X\n", status);
			ZwClose(hFile);
			return false;
		}

		

		ZwClose(hFile);

		return true;
	}
}


namespace ksocket {

	SOCKET g_lisentSocket = 0;//���ڹر�accept���� ֹͣaccept
	bool g_stop = false;
	//����C++17�Ľṹ����
	struct pp {

		char* p1;
		char* p2;
	};

#if DBG
#define printk(...)do{DbgPrintEx(77,0,__VA_ARGS__);}while(0);
#else
#define printk(...)do{}while(0);
#endif

	
	static bool createGetRequest(const char* hostname, const char* path, __inout char* request) {

		if (request == nullptr) return false;
		strcpy(request, "GET ");
		strcat(request, path);
		strcat(request, " HTTP/1.1\r\n");
		strcat(request, "Host: ");
		strcat(request, hostname);
		strcat(request, "\r\n");
		strcat(request, "Connection: close\r\n\r\n");

		printk("request str-> %s", request);
		
		return true;
		
	}


	static int findChar(const char* str, char tagrt_c) {

		for (int i = 0; i < strlen(str); i++) if (str[i] == tagrt_c) return i;

		return -1;
	}

	using strstrfn = char* (*)(const char*,const char*);
	static pp getHostAndPath(const char* url) {

		UNICODE_STRING funcName{ 0 };
		RtlInitUnicodeString(&funcName, L"strstr");

		//must use in this way cause we include vc path!
		auto ___strstr = (strstrfn)(MmGetSystemRoutineAddress(&funcName));
		//�Ȼ�ȡhostname��path
		char* _hostname = ___strstr(url, "://");

		if (_hostname == nullptr) {
			return { 0 };
		}

		_hostname += 3;//����http://sdsd/��Щ�ַ�

		//������������β
		size_t hostnameLen = findChar(_hostname, '/');
		if (hostnameLen == -1) return { 0 };

		//����Path·���ַ���
		size_t pathLen = strlen(_hostname) - hostnameLen;

		char* path = (char*)ExAllocatePool(PagedPool, pathLen + 5);
		if (path == nullptr) return{ 0 };

		memset(path, 0, pathLen + 5);
		memcpy(path, _hostname + hostnameLen, pathLen);
		path[pathLen] =	'\0';

		//����host�ַ���
		char* hostname = (char*)ExAllocatePool(PagedPool, hostnameLen + 5);
		if (hostname == nullptr) {
			ExFreePool(path);
			return { 0 };
		}

		memset(hostname, 0, hostnameLen + 5);
		memcpy(hostname, _hostname, hostnameLen);
		hostname[hostnameLen] = '\0';

		return { hostname,path };

	}

	bool init() {

		if (!NT_SUCCESS(KsInitialize())) {

			return false;
		}
		else return true;

	}

	void destory() {
		g_stop = true;//������R3�������� ����ж�� ��Ȼж�ز���
		
		KsDestroy();//����ط�������

	}

	//base�ǻ��� ������
	auto _atoi(char* asci, int base) -> LONG64 {
		ANSI_STRING tidString{ 0 };
		UNICODE_STRING tidUString{ 0 };
		//driver char*->intger
		printk("asci ->%p\t", asci);
		printk("asci ->%s\r\n", asci);
		RtlInitAnsiString(&tidString, asci);
		if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&tidUString,
			&tidString,
			true))) {
			LONG64 tid = 0;
			PWCHAR endPointer = nullptr;
			NTSTATUS status=RtlUnicodeStringToInt64(&tidUString, base, &tid, &endPointer);
			if(NT_SUCCESS(status))
			//�ͷ��ڴ�
				RtlFreeUnicodeString(&tidUString);
			return tid;
		}

		return 0;
	}

	//RETURN 0 fail

	size_t getContentLength(const char* url,const char* port)
	{

		auto [hostname, path] = getHostAndPath(url);
		int result;
		if (!hostname || !path) return 0;

		//Ȼ�󴴽�һ��http get����
		char* request = (char*)ExAllocatePool(PagedPool, PAGE_SIZE);
		if (request == nullptr) {

			ExFreePool(hostname);
			ExFreePool(path);
			return 0;
		}

		memset(request, 0, PAGE_SIZE);

		createGetRequest(hostname, path, request);


		//�Ȼ�ȡHTTP��Ӧͷ��
		char recv_buffer[1024] = { 0 };

		struct addrinfo hints = { 0 };
		//hints.ai_flags |= AI_CANONNAME;
		//hints.ai_family = AF_UNSPEC;
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;//TCP

		//���ǻ�ȡ���url����Ϣ��
		struct addrinfo* res;
		result = getaddrinfo(hostname, port, &hints, &res);
		if (!NT_SUCCESS(result)) {
			freeaddrinfo(res);
			printk("failed to get addr info\r\n");
			return 0;
		}
		int sockfd;
		sockfd = socket_connection(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		result = connect(sockfd, res->ai_addr, (int)res->ai_addrlen);
		result = send(sockfd, request, strlen(request) + 1, 0);
		result = recv(sockfd, recv_buffer, sizeof(recv_buffer), 0);
		recv_buffer[sizeof(recv_buffer) - 1] = '\0';
		//����
		freeaddrinfo(res);
		char* findStr=strstr(recv_buffer, "Content-Length:");
		if (findStr == nullptr) {
			ExFreePool(hostname);
			ExFreePool(path);
			closesocket(sockfd);
			return 0;
		};

		//����
		findStr += strlen("Content-Length:") + 1;

		ULONG len= (ULONG)_atoi(findStr, 10);
		
		closesocket(sockfd);

		return len;
	}

	bool getHttpContent(const char* url,char* buf, size_t bufSize, const char* port)
	{
		int result;
		auto [hostname, path] = getHostAndPath(url);
		
		if (!hostname || !path) return false;

		//Ȼ�󴴽�һ��http get����
		char* request = (char*)ExAllocatePool(PagedPool, PAGE_SIZE);
		if (request == nullptr) {

			ExFreePool(hostname);
			ExFreePool(path);
			return false;
		}

		memset(request, 0, PAGE_SIZE);

		createGetRequest(hostname, path, request);


		//�Ȼ�ȡ���HTTP��Ӧͷ����С
		size_t httpContentSize = getContentLength(url, port);
		if (httpContentSize == 0 || httpContentSize>bufSize) {
			printk("content length err\r\n");
			return false;
		}

		char* recv_buffer =(char*)ExAllocatePool(PagedPool, 
			httpContentSize + PAGE_SIZE);
		if (recv_buffer == nullptr) {
			ExFreePool(hostname);
			ExFreePool(path);
			ExFreePool(request);
			printk("failed to alloc memory!\r\n");
			return false;

		}



		struct addrinfo hints = { 0 };
		hints.ai_flags |= AI_CANONNAME;
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;//TCP

		//���ǻ�ȡ���url����Ϣ��
		struct addrinfo* res;
		result = getaddrinfo(hostname, port, &hints, &res);
		if (!NT_SUCCESS(result)) {

			ExFreePool(hostname);
			ExFreePool(path);
			ExFreePool(request);
			ExFreePool(recv_buffer);
			printk("failed to get addr info\r\n");
			return false;
		}
		int sockfd;
		sockfd = socket_connection(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		result = connect(sockfd, res->ai_addr, (int)res->ai_addrlen);

		//��Ϊtcp�������� ���Ե���ѭ��

		result = send(sockfd, request, strlen(request)+1, 0);


		size_t total_received = 0;
		size_t left_to_receive = httpContentSize + PAGE_SIZE;
		while (total_received < httpContentSize) {

			result = recv(sockfd, recv_buffer+ total_received, left_to_receive - total_received, 0);
			
			if (result <= 0) break;
			total_received += result;
		}
	

		//���ҵ�content��ʼ�ĵ�ַ
		char* contentStart = strstr(recv_buffer, "\r\n\r\n");
		if (contentStart == nullptr) {


			ExFreePool(hostname);
			ExFreePool(path);
			ExFreePool(request);
			ExFreePool(recv_buffer);
			printk("content fromat error!\r\n");
			closesocket(sockfd);
			return false;
		}

		memcpy(buf, contentStart + 4, httpContentSize);
		
		ExFreePool(recv_buffer);


		closesocket(sockfd);


		return true;
	}


	static SOCKET create_listen_socket(short _server_port)
	{
		SOCKADDR_IN address{ };

		address.sin_family = AF_INET;
		address.sin_port = htons(_server_port);

		const auto listen_socket = socket_listen(AF_INET, SOCK_STREAM, 0);
		if (listen_socket == INVALID_SOCKET)
		{
			printk("Failed to create listen socket.");
			return INVALID_SOCKET;
		}

		if (bind(listen_socket, (SOCKADDR*)&address, sizeof(address)) == SOCKET_ERROR)
		{
			printk("Failed to bind socket.");

			closesocket(listen_socket);
			return INVALID_SOCKET;
		}

		if (listen(listen_socket, 10) == SOCKET_ERROR)
		{
			printk("Failed to set socket mode to listening.");

			closesocket(listen_socket);
			return INVALID_SOCKET;
		}

		return listen_socket;
	}


#pragma warning(disable :4302)
#pragma warning(disable :4311)
	bool createServer(short port,acceptCallback_t callback)
	{
		//ֻ�ܴ���һ��������
		if (g_lisentSocket != 0) {

			printk("now only support one server!\r\n");
			return false;
		}
		//���봴�����߳�
		//c++ 17 �ṹ���� �ǵ������ڴ洫�ݲ��� ��Ȼ�ģ�
		auto sBonding = (pp*)ExAllocatePool(PagedPool, sizeof pp);
		if (sBonding == nullptr) {
			return false;
		}

		sBonding->p1 = (char*)port;
		sBonding->p2 = (char*)callback;

		helper::_thread serverThread([](void* context) {

			auto [_port, _callback] = *(pp*)context;
			//�����ڴ�
			ExFreePool(context);


			int listen_socket = create_listen_socket((short)_port);

			if (listen_socket == INVALID_SOCKET) {
				printk("failed to create and lisent!");
				return;
			}

			g_lisentSocket = listen_socket;
			//��ʼѭ������ �ǵö��߳�
			while (true) {

				sockaddr  socket_addr{ };
				socklen_t socket_length{ };

				
				const auto client_connection = accept(listen_socket, &socket_addr, &socket_length);
				if (client_connection == INVALID_SOCKET)
				{
					
					printk("Failed to accept client connection.");
					break;
				}

				if (g_stop) {
					
					
					closesocket(client_connection);
					closesocket(g_lisentSocket);
					g_lisentSocket = 0;
					
					break;
				}
				//c++ 17 �ṹ���� �ǵ������ڴ洫�ݲ��� ��Ȼ�ģ�
				auto sBonding=(pp*)ExAllocatePool(PagedPool, sizeof pp);
				if (sBonding == nullptr) return;
				sBonding->p1 = (char*)client_connection;
				sBonding->p2 = (char*)_callback;

				helper::_thread connectThread([](void* context) {
					
					auto [_client_connection, ___callback] = *(pp*)context;
					ExFreePool(context);

					printk("New connection. _client_socket->%d,__callback->%p"
						,_client_connection,___callback);

					Packet packet{ };
					while (true)
					{
						const auto result = recv((int)_client_connection, (void*)&packet, sizeof(packet), 0);
						if (result <= 0)
							break;

						//buf��С����
						if (result != sizeof(Packet))
							continue;

						//ħ������
						if (packet.header.magic != packet_magic)
							continue;

						const auto packet_result = ((acceptCallback_t)___callback)(packet.data);
						if (packet_result) {
							//�ɹ� ���ͳɹ���
							auto p = Packet{ .header = {0,PacketStatus::success} };
							send((int)_client_connection, &p, sizeof(packet), 0);
						}
						else {
							//������ʧ��
							auto p=Packet{ .header = {0,PacketStatus::failure} };
							send((int)_client_connection, &p, sizeof(packet), 0);
						}


					}

					printk("Connection closed.");
					closesocket((int)_client_connection);


					}, (void*)sBonding);

				if (!NT_SUCCESS(connectThread.getStatus()))
				{
					printk("Failed to create thread for handling client connection.");

					closesocket(client_connection);
					break;
				}

				ZwClose(connectThread.getHandle());

			}



			}, (void*)sBonding);


		if (!NT_SUCCESS(serverThread.getStatus())) {
			printk("failed to create server thread!\r\n");
			return false;
		}

		ZwClose(serverThread.getHandle());

		return true;
	}



#pragma warning(default :4302)
#pragma warning(default :4311)

	void destoryServer()
	{
		if(g_lisentSocket!=0)
			closesocket(g_lisentSocket);
		g_lisentSocket = 0;
	}

}

#pragma warning(default :4996)