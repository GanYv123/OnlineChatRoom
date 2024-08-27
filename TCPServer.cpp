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
	// ���� SO_REUSEADDR ѡ�� ȡ��time_wait��Ӱ��
	int optval = 1;
	if (setsockopt(m_server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
		std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
	}
	//�ȱ�������׽���
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

	// ����epoll
	m_epfd = epoll_create(1024);
	if (m_epfd == -1) {
		std::cerr << "epoll create failed: " << strerror(errno) << std::endl;
		close(m_server_sock);
		return;
	}
	m_pAll_events = new epoll_event[100];
	// ע��������׽���
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
				// �����µ�����
				int client_sock = accept(m_server_sock, (struct sockaddr*)&m_client_addr, &m_client_addr_len);
				if (client_sock == -1) {
					std::cerr << "Client accept failed: " << strerror(errno) << std::endl;
					continue;
				}

				m_epoll_event.events = EPOLLIN;
				m_epoll_event.data.fd = client_sock;
				if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, client_sock, &m_epoll_event) == -1) {//���ʧ��
					std::cerr << "epoll_ctl failed: " << strerror(errno) << std::endl;
					close(client_sock);
				}
				else {//��� epoll �ɹ�
					//��ʼSOCKET��״̬
					 
					//g_clientStatus_map[client_sock] = false;
					unsigned int client_id = assignClientId();
					g_clientID_map[client_sock] = client_id;
					//����ÿ��socket����
					enableTCPKeepAlive(client_sock);
					std::cout << "Client is connected! clientId: " << client_id << std::endl;
				}
			}
			else {
				// ����ͻ�������
				int client_sock = m_pAll_events[i].data.fd;
				memset(m_recvBuffer, 0, sizeof(m_recvBuffer));
				ssize_t len_read = read(client_sock, m_recvBuffer, sizeof(m_recvBuffer));

				if (len_read == -1) {
					std::cerr << "Read failed: " << strerror(errno) << std::endl;
					handleClientDisconnection(client_sock);
				}
				else if (len_read == 0) {
					// �ͻ��˹ر�����
					handleClientDisconnection(client_sock);
					//g_clientStatus_map.erase(client_sock); // �Ƴ�״̬��¼
				}
				else {
					if (g_clientFileMap[client_sock].isTransferring) {
						// ��ǰ�ͻ��������ϴ��ļ�����
						receiveFileData(client_sock, m_recvBuffer, len_read); // �����ļ�����
						//receiveFile(client_sock);

					}
					else {
						//��ͨ��Ϣ
						handleClientMessage(client_sock, m_recvBuffer);
					}
				}
			}
		}
	}
}

void TCPServer::receiveFileData(int client_sock, const char* data, ssize_t len) {
	// ������д�뵽�ļ�
	// ʹ��һ���ļ������߻��������洢�ļ�����
		// ���Ҷ�Ӧ�ͻ��˵� FileData
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

	// �����յ�������д�뵽�ļ�
	fileData.outFile->write(data, len);

	// �����ѽ����ֽ���
	fileData.bytesReceived += len;

	// ����Ƿ�������
	if (fileData.bytesReceived >= fileData.fileSize) {
		fileData.outFile->close(); // �ر��ļ���
		fileData.isTransferring = false; // ��Ǵ������
		//��������򷵻�һ���ļ��б�
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
	std::ofstream outFile(fileName, std::ios::binary);//������д��

	if (!outFile.is_open()) {
		std::cerr << "Failed to open file for writing: " << fileName << std::endl;
		return;
	}

	qint64 bytesReceived = 0; //�����˶���
	while (bytesReceived < fileSize) { //һֱ�ְ����� ֱ��ȫ������
		ssize_t len_read = read(client_sock, buffer, std::min(bufferSize, fileSize - bytesReceived));
		//�����ȡ �� ���һ���� ��С���ܲ����㻺������С ��С�Ľ��ж�ȡ
		if (len_read <= 0) {
			std::cerr << "Error receiving file data: " << strerror(errno) << std::endl;
			break;
		}
		outFile.write(buffer, len_read); //ֱ�ӽ�����������д���ļ�
		bytesReceived += len_read; //д����ɺ�����ӽ��յ����ļ��ֽ�����
	}

	outFile.close();
	std::cout << "File received and saved: " << fileName << std::endl;
	std::lock_guard<std::mutex> lockGuard(myMutex);
	//g_clientStatus_map[client_sock] = false; //�������ȡ������״̬
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
	else if (ret != len) {//д�벻��ȫ
		std::cerr << "Partial write to client socket " << client_sock << ": " << ret << " bytes sent, " << len << " bytes expected" << std::endl;
	}
	else {
		//
	}
}

void TCPServer::startFileSending(int client_sock, const std::string& fileName, qint64 fileSize) {
	// ���ļ����ж�ȡ
	std::ifstream file(fileName, std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "cannot open file: " << fileName << std::endl;
		return;
	}

	char buffer[1024 * 64]; // 64KB ������
	qint64 totalBytesSent = 0;

	while (totalBytesSent < fileSize) {
		// ���ļ��ж�ȡ����
		file.read(buffer, sizeof(buffer));
		std::streamsize bytesRead = file.gcount();

		// ���Ͷ�ȡ��������
		if (bytesRead > 0) {
			ssize_t len_sent = send(client_sock, buffer, bytesRead, 0);
			if (len_sent == -1) {
				std::cerr << __LINE__ << "error:"<< strerror(errno) << std::endl;
				break;
			}
			totalBytesSent += len_sent;
		}
		else {
			break; // �ļ���ȡ���
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
	//���߷��͹������ı������ض����ı���Ϣ
	if(sizeof(msg)<MAX_BUFFERSIZE)
		for (auto& pair : g_clientID_map) {
			write(pair.first, msg, strlen(msg) + 1);
		}
}

void TCPServer::handleClientMessage(int client_sock, const char* message) {
	std::string response;
	//std::cerr << "Handle message:" << message << std::endl;
	if (strcmp(message, "code:1") == 0 || strcmp(message, "code:0") == 0) { //�������ߺ�������Ϣ�򷵻�ʵʱ����
		// ���ص�ǰ���пͻ��˵� ID
		std::string allClientIds = "IDs:";
		for (const auto& pair : g_clientID_map) {
			if ((pair.first == client_sock) && (strcmp(message, "code:0") == 0)) continue;
			allClientIds += std::to_string(pair.second) + ",";
		}
		response = allClientIds;

		//�����ļ��б���Ϣ
		if(strcmp(message, "code:1") == 0)
			sendMsgToClient(client_sock, formatFileList(getFileList(DEFAULT_FILE_PATH, client_sock)));
	}
	else if (strstr(message, "UPLOAD") == message) {
		// �����ļ���Ϣ
		std::string fileInfo(message);
		std::string fileName = TCPServer::parseFileName(fileInfo);
		fileName.insert(0, DEFAULT_FILE_PATH);
		qint64 fileSize = TCPServer::parseFileSize(fileInfo);

		FileData fileData;
		fileData.outFile = std::unique_ptr<std::ofstream>(new std::ofstream(fileName, std::ios::binary));
		fileData.bytesReceived = 0;
		fileData.fileSize = fileSize;
		fileData.fileName = fileName;
		fileData.isTransferring = true; // ����Ϊ����״̬

		if (!fileData.outFile->is_open()) {
			std::cerr << "Failed to open file for writing: " << fileName << std::endl;
			return;
		}
		// ����ʼ����Ľṹ����� g_clientFileMap
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
		sendMsgToClient(client_sock, "fileSize:" + std::to_string(fileSize));//�ȷ��ʹ�С
		//��ʼ����һ���̴߳����ļ�
		std::thread t_sendFileData(&TCPServer::startFileSending, this, client_sock,fileName,fileSize);
		t_sendFileData.detach();
	}
	else {
		// ������Ϣ����
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
		// �ų���ǰĿ¼ "." �͸�Ŀ¼ ".."
		if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		std::string filePath = directory + entry->d_name;
		//std::cout << "Checking file: " << filePath << std::endl;
		if (stat(filePath.c_str(), &fileStat) == 0 && S_ISREG(fileStat.st_mode)) {
			auto fileName = entry->d_name;
			auto fileSize = fileStat.st_size;
			// �ϴ�����Ϣ��Ҫ�������ط���ȡ������ʹ�ÿ��ַ�����Ϊռλ��
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

	// ����TCP����
	if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0) {
		std::cerr << "Error setting SO_KEEPALIVE: " << strerror(errno) << std::endl;
	}

	// ���ñ���̽���ʱ�������룩
	optval = 60; // 1����
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, optlen) < 0) {
		std::cerr << "Error setting TCP_KEEPIDLE: " << strerror(errno) << std::endl;
	}

	// ���ñ���̽���Ƶ�ʣ��룩
	optval = 10; // ÿ10�뷢��һ�α���̽��
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, optlen) < 0) {
		std::cerr << "Error setting TCP_KEEPINTVL: " << strerror(errno) << std::endl;
	}

	// ����̽��ʧ�ܶ��ٴκ�Ͽ�����
	optval = 5; // ̽��5��ʧ�ܺ�Ͽ�����
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
