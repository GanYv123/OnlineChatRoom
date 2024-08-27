#include "TCPServer.h"

TCPServer::TCPServer() {
	memset(&m_server_addr, 0, sizeof(m_server_addr));
	m_server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	m_server_addr.sin_family = AF_INET;
	m_server_addr.sin_port = htons(2233);

	m_server_sock = socket(PF_INET, SOCK_STREAM, 0);

	if (m_server_sock == -1) {
		std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
		return;
	}
	// 设置 SO_REUSEADDR 选项 取消time_wait的影响
	int optval = 1;
	if (setsockopt(m_server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
		std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
	}
	//先保活监听套接字
	enableTCPKeepAlive(m_server_sock);

	if (bind(m_server_sock, (struct sockaddr*)&m_server_addr, sizeof(m_server_addr)) == -1) {
		std::cerr << "Bind failed: " << strerror(errno) << std::endl;
		close(m_server_sock);
		return;
	}

	std::cout << "Waiting for connections..." << std::endl;
	if (listen(m_server_sock, 5) == -1) {
		std::cerr << "Listen failed: " << strerror(errno) << std::endl;
		close(m_server_sock);
		return;
	}

	// 创建epoll
	m_epfd = epoll_create(1024);
	if (m_epfd == -1) {
		std::cerr << "epoll create failed: " << strerror(errno) << std::endl;
		close(m_server_sock);
		return;
	}
	m_pAll_events = new epoll_event[100];
	// 注册服务器套接字
	m_epoll_event.events = EPOLLIN;
	m_epoll_event.data.fd = m_server_sock;
	epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_server_sock, &m_epoll_event);
}

TCPServer::~TCPServer() {
	delete[] m_pAll_events;
	close(m_server_sock);
	close(m_epfd);
}

unsigned int TCPServer::assignClientId() {
	return g_client_id_counter++;
}

void TCPServer::run() {
	while (true) {
		m_event_cnt = epoll_wait(m_epfd, m_pAll_events, 100, 1000);
		if (m_event_cnt == -1) {
			std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
			break;
		}
		if (m_event_cnt == 0) continue;

		for (int i = 0; i < m_event_cnt; ++i) {
			if (m_pAll_events[i].data.fd == m_server_sock) {
				// 处理新的连接
				int client_sock = accept(m_server_sock, (struct sockaddr*)&m_client_addr, &m_client_addr_len);
				if (client_sock == -1) {
					std::cerr << "Client accept failed: " << strerror(errno) << std::endl;
					continue;
				}

				m_epoll_event.events = EPOLLIN;
				m_epoll_event.data.fd = client_sock;
				if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, client_sock, &m_epoll_event) == -1) {//添加失败
					std::cerr << "epoll_ctl failed: " << strerror(errno) << std::endl;
					close(client_sock);
				}
				else {//添加 epoll 成功
					//初始SOCKET化状态
					 
					//g_clientStatus_map[client_sock] = false;
					unsigned int client_id = assignClientId();
					g_clientID_map[client_sock] = client_id;
					//保活每个socket连接
					enableTCPKeepAlive(client_sock);
					std::cout << "Client is connected! clientId: " << client_id << std::endl;
				}
			}
			else {
				// 处理客户端数据
				int client_sock = m_pAll_events[i].data.fd;
				memset(m_recvBuffer, 0, sizeof(m_recvBuffer));
				ssize_t len_read = read(client_sock, m_recvBuffer, sizeof(m_recvBuffer));

				if (len_read == -1) {
					std::cerr << "Read failed: " << strerror(errno) << std::endl;
					handleClientDisconnection(client_sock);
				}
				else if (len_read == 0) {
					// 客户端关闭连接
					handleClientDisconnection(client_sock);
					//g_clientStatus_map.erase(client_sock); // 移除状态记录
				}
				else {
					if (g_clientFileMap[client_sock].isTransferring) {
						// 当前客户端正在上传文件数据
						receiveFileData(client_sock, m_recvBuffer, len_read); // 处理文件数据
						//receiveFile(client_sock);

					}
					else {
						//普通消息
						handleClientMessage(client_sock, m_recvBuffer);
					}
				}
			}
		}
	}
}

void TCPServer::receiveFileData(int client_sock, const char* data, ssize_t len) {
	// 将数据写入到文件
	// 使用一个文件流或者缓冲区来存储文件数据
		// 查找对应客户端的 FileData
	auto it = g_clientFileMap.find(client_sock);
	if (it == g_clientFileMap.end()) {
		std::cerr << "Client file data not found for socket: " << client_sock << std::endl;
		return;
	}

	FileData& fileData = it->second;

	if (!fileData.isTransferring) {
		std::cerr << "No file transfer in progress for socket: " << client_sock << std::endl;
		return;
	}

	// 将接收到的数据写入到文件
	fileData.outFile->write(data, len);

	// 更新已接收字节数
	fileData.bytesReceived += len;

	// 检查是否接收完成
	if (fileData.bytesReceived >= fileData.fileSize) {
		fileData.outFile->close(); // 关闭文件流
		fileData.isTransferring = false; // 标记传输完成
		//接收完成则返回一个文件列表
		//std::cerr <<"current file nums:" << getFileList(DEFAULT_FILE_PATH, client_sock).size();
		//std::cerr << formatFileList(getFileList(DEFAULT_FILE_PATH, client_sock));
		sendMsgToAllClient(formatFileList(getFileList(DEFAULT_FILE_PATH, client_sock)).c_str());
		std::cout << "File received and saved: " << fileData.fileName << std::endl;
	}
}

#if 0
void TCPServer::receiveFileData(int client_sock) {

}

void TCPServer::startFileReceiving(int client_sock, const std::string& fileName, qint64 fileSize) {
	receiveFile(client_sock, fileName, fileSize);
	std::cerr << "detach thread upload file" << std::endl;
}

void TCPServer::receiveFile(int client_sock, const std::string& fileName, qint64 fileSize) {
//#if 0
	const qint64 bufferSize = 1024 * 1024; // 1MB
	char buffer[bufferSize];
	std::ofstream outFile(fileName, std::ios::binary);//二进制写入

	if (!outFile.is_open()) {
		std::cerr << "Failed to open file for writing: " << fileName << std::endl;
		return;
	}

	qint64 bytesReceived = 0; //接收了多少
	while (bytesReceived < fileSize) { //一直分包接收 直到全部读完
		ssize_t len_read = read(client_sock, buffer, std::min(bufferSize, fileSize - bytesReceived));
		//如果读取 到 最后一个包 大小可能不满足缓冲区大小 按小的进行读取
		if (len_read <= 0) {
			std::cerr << "Error receiving file data: " << strerror(errno) << std::endl;
			break;
		}
		outFile.write(buffer, len_read); //直接将读到的数据写道文件
		bytesReceived += len_read; //写入完成后就增加接收到的文件字节数量
	}

	outFile.close();
	std::cout << "File received and saved: " << fileName << std::endl;
	std::lock_guard<std::mutex> lockGuard(myMutex);
	//g_clientStatus_map[client_sock] = false; //传输完毕取消传输状态
//#endif//0

}
#endif// 0

void TCPServer::sendMsgToClient(int client_sock, const std::string& msg) {
	ssize_t len = msg.length();
	if (len >= MAX_BUFFERSIZE) return;
	ssize_t ret = write(client_sock, msg.c_str(), len);
	if (ret == -1) {
		std::cerr << "Failed to send message to client socket " << client_sock << ": " << strerror(errno) << std::endl;
	}
	else if (ret != len) {//写入不完全
		std::cerr << "Partial write to client socket " << client_sock << ": " << ret << " bytes sent, " << len << " bytes expected" << std::endl;
	}
	else {
		//
	}
}

void TCPServer::startFileSending(int client_sock, const std::string& fileName, qint64 fileSize) {
	// 打开文件进行读取
	std::ifstream file(fileName, std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "cannot open file: " << fileName << std::endl;
		return;
	}

	char buffer[1024 * 64]; // 64KB 缓冲区
	qint64 totalBytesSent = 0;

	while (totalBytesSent < fileSize) {
		// 从文件中读取数据
		file.read(buffer, sizeof(buffer));
		std::streamsize bytesRead = file.gcount();

		// 发送读取到的数据
		if (bytesRead > 0) {
			ssize_t len_sent = send(client_sock, buffer, bytesRead, 0);
			if (len_sent == -1) {
				std::cerr << __LINE__ << "error:"<< strerror(errno) << std::endl;
				break;
			}
			totalBytesSent += len_sent;
		}
		else {
			break; // 文件读取完毕
		}
	}

	file.close();
	std::cerr<< "file send done!: " << fileName << std::endl;
}



void TCPServer::handleClientDisconnection(int client_sock) {
	if (g_clientID_map.find(client_sock) != g_clientID_map.end()) {
		std::cout << "Client disconnected, ID: " << g_clientID_map[client_sock] << std::endl;
		g_clientID_map.erase(client_sock);
		std::cout << "Remaining quantity: " << g_clientID_map.size() << std::endl;
	}
	epoll_ctl(m_epfd, EPOLL_CTL_DEL, client_sock, nullptr);
	close(client_sock);
}

std::string TCPServer::formatFileList(const std::list<File>& fileList) {
	std::ostringstream oss;
	oss << "FileInfo>>";
	for (const auto& file : fileList) {
		oss << file.toString() << "\n";
	}
	oss << "<<FileInfoEnd";
	return oss.str();
}

void TCPServer::sendMsgToAllClient(const char* msg) {
	//更具发送过来的文本发送特定的文本信息
	if(sizeof(msg)<MAX_BUFFERSIZE)
		for (auto& pair : g_clientID_map) {
			write(pair.first, msg, strlen(msg) + 1);
		}
}

void TCPServer::handleClientMessage(int client_sock, const char* message) {
	std::string response;
	//std::cerr << "Handle message:" << message << std::endl;
	if (strcmp(message, "code:1") == 0 || strcmp(message, "code:0") == 0) { //发送上线和下线信息则返回实时人数
		// 返回当前所有客户端的 ID
		std::string allClientIds = "IDs:";
		for (const auto& pair : g_clientID_map) {
			if ((pair.first == client_sock) && (strcmp(message, "code:0") == 0)) continue;
			allClientIds += std::to_string(pair.second) + ",";
		}
		response = allClientIds;

		//发送文件列表信息
		if(strcmp(message, "code:1") == 0)
			sendMsgToClient(client_sock, formatFileList(getFileList(DEFAULT_FILE_PATH, client_sock)));
	}
	else if (strstr(message, "UPLOAD") == message) {
		// 解析文件信息
		std::string fileInfo(message);
		std::string fileName = TCPServer::parseFileName(fileInfo);
		fileName.insert(0, DEFAULT_FILE_PATH);
		qint64 fileSize = TCPServer::parseFileSize(fileInfo);

		FileData fileData;
		fileData.outFile = std::unique_ptr<std::ofstream>(new std::ofstream(fileName, std::ios::binary));
		fileData.bytesReceived = 0;
		fileData.fileSize = fileSize;
		fileData.fileName = fileName;
		fileData.isTransferring = true; // 设置为传输状态

		if (!fileData.outFile->is_open()) {
			std::cerr << "Failed to open file for writing: " << fileName << std::endl;
			return;
		}
		// 将初始化后的结构体存入 g_clientFileMap
		g_clientFileMap[client_sock] = std::move(fileData);
		std::cout << "Started receiving file: " << fileName << " from client " << client_sock << std::endl;
	}
	else if (strstr(message, "<download>:") == message) {

		std::string fileName(message);
		fileName.erase(0,sizeof("<download>:")-1);
		std::cerr << "download file name:" << fileName << std::endl;
		fileName.insert(0, DEFAULT_FILE_PATH);
		qint64 fileSize = TCPServer::getFileSize(fileName);
		std::cerr << "download file size:" << fileSize << std::endl;
		sendMsgToClient(client_sock, "fileSize:" + std::to_string(fileSize));//先发送大小
		//开始单开一个线程传输文件
		std::thread t_sendFileData(&TCPServer::startFileSending, this, client_sock,fileName,fileSize);
		t_sendFileData.detach();
	}
	else {
		// 其他信息回文
		char resultMsg[MAX_BUFFERSIZE+1024] = { 0 };
		snprintf(resultMsg, sizeof(resultMsg), "(%d)send:%s", g_clientID_map[client_sock], message);
		response = resultMsg;
	}
	std::cout << response << std::endl;
	if (!response.empty())
		sendMsgToAllClient(response.c_str());
}

std::list<File> TCPServer::getFileList(const std::string& directory,int socket) {
	std::list<File> fileList;
	DIR* dir;
	struct dirent* entry;
	struct stat fileStat;

	std::cout << "Attempting to open directory: " << directory << std::endl;

	if ((dir = opendir(directory.c_str())) == nullptr) {
		std::cerr << "Unable to open directory: " << directory << std::endl;
		return fileList;
	}

	while ((entry = readdir(dir)) != nullptr) {
		// 排除当前目录 "." 和父目录 ".."
		if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		std::string filePath = directory + entry->d_name;
		//std::cout << "Checking file: " << filePath << std::endl;
		if (stat(filePath.c_str(), &fileStat) == 0 && S_ISREG(fileStat.st_mode)) {
			auto fileName = entry->d_name;
			auto fileSize = fileStat.st_size;
			// 上传者信息需要从其他地方获取，这里使用空字符串作为占位符
			fileList.emplace_back(filePath, fileName, fileSize, "<---->");
		}
		else {
			std::cerr << "Failed to get file status for: " << filePath << std::endl;
		}
	}
	closedir(dir);
	return fileList;
}

void TCPServer::enableTCPKeepAlive(int sockfd) {
	int optval = 1;
	socklen_t optlen = sizeof(optval);

	// 启用TCP保活
	if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0) {
		std::cerr << "Error setting SO_KEEPALIVE: " << strerror(errno) << std::endl;
	}

	// 设置保活探测的时间间隔（秒）
	optval = 60; // 1分钟
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, optlen) < 0) {
		std::cerr << "Error setting TCP_KEEPIDLE: " << strerror(errno) << std::endl;
	}

	// 设置保活探测的频率（秒）
	optval = 10; // 每10秒发送一次保活探测
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, optlen) < 0) {
		std::cerr << "Error setting TCP_KEEPINTVL: " << strerror(errno) << std::endl;
	}

	// 设置探测失败多少次后断开连接
	optval = 5; // 探测5次失败后断开连接
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, optlen) < 0) {
		std::cerr << "Error setting TCP_KEEPCNT: " << strerror(errno) << std::endl;
	}
}

File::File(const std::string& p, const std::string& n, qint64 s, const std::string& u)
	: path(p), name(n), size(s), uploader(u)
{
	
}

std::string File::toString() const {
	std::ostringstream oss;
	oss << name << "," << uploader << "," << size;
	return oss.str();
}
