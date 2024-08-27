#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unordered_map>
#include <thread>
#include <sstream>
#include <mutex>
#include <fstream>
#include <memory>
#include <dirent.h>
#include <sys/stat.h>
#include <list>

#define MAX_BUFFERSIZE 1024*1024
#define DEFAULT_FILE_PATH "/root/projects/QQServer/bin/x64/Debug/myFiles/"

typedef long long qint64;


struct FileData {
	std::unique_ptr<std::ofstream> outFile;   // ʹ������ָ������ļ���
	std::unique_ptr<std::ifstream> inFile;   // ʹ������ָ������ļ���
	qint64 bytesReceived;   // �ѽ����ֽ���
	qint64 fileSize;        // �ļ��ܴ�С
	std::string fileName;   // �ļ���
	bool isTransferring{ false };   // �ļ�����״̬
};

struct File {
	std::string path;
	std::string name;
	qint64 size;
	std::string uploader; // �ϴ�����Ϣ

	// ���캯��
	File(const std::string& p, const std::string& n, qint64 s, const std::string& u);
	std::string toString()const;
		
	
};

class TCPServer {
public:
	TCPServer();
	~TCPServer();
	void run();  // Added a run method to start the server loop

	static std::string parseFileName(const std::string& fileInfo) {
		std::istringstream stream(fileInfo);
		std::string token;
		std::getline(stream, token, ','); // ���Ե�һ������ (UPLOAD)
		std::getline(stream, token, ','); // ��ȡ�ļ���
		return token;
	}

	static qint64 getFileSize(const std::string& fileName) {
		std::ifstream file(fileName, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			std::cerr << "cannot open file: " << fileName << std::endl;
			return -1;
		}
		long long fileSize = static_cast<long long>(file.tellg());
		file.close();
		return fileSize;
	}

	static qint64 parseFileSize(const std::string& fileInfo) {
		std::istringstream stream(fileInfo);
		std::string token;
		std::getline(stream, token, ','); // ���Ե�һ������ (UPLOAD)
		std::getline(stream, token, ','); // �����ļ���
		std::getline(stream, token, ','); // ��ȡ�ļ���С
		return std::stoll(token);
	}

	static std::string formatFileSize(qint64 size) {
		double fileSize = static_cast<double>(size);
		std::string unit;
		if (fileSize < 1024) {
			unit = "B";
		}
		else if (fileSize < 1024 * 1024) {
			fileSize /= 1024;
			unit = "KB";
		}
		else if (fileSize < 1024 * 1024 * 1024) {
			fileSize /= (1024 * 1024);
			unit = "MB";
		}
		else {
			fileSize /= (1024 * 1024 * 1024);
			unit = "GB";
		}
		char buffer[50];
		snprintf(buffer, sizeof(buffer), "%.2f %s", fileSize, unit.c_str());
		return std::string(buffer);
	}

	// ���ļ��б�ת��Ϊһ��ƴ�Ӻ���ַ���
	std::string formatFileList(const std::list<File>& fileList);

	void sendMsgToAllClient(const char*);
	void handleClientMessage(int client_sock, const char* message);
	std::list<File> getFileList(const std::string& directory, int socket);
	std::list<File> getFileList(const std::string& directory);
	void handleClientDisconnection(int client_socket);

	void enableTCPKeepAlive(int sockfd);

	void receiveFileData(int client_sock, const char* data, ssize_t len);
	void receiveFileData(int client_sock);

	void startFileReceiving(int client_sock, const std::string& fileName, qint64 fileSize);
	//�����ļ�����
	void receiveFile(int client_sock, const std::string& fileName, qint64 fileSize);
	void sendMsgToClient(int client_sock, const std::string& msg);
	//�����ļ����ݵĽ���

	//�ļ�����
	void startFileSending(int client_sock, const std::string& fileName, qint64 fileSize);
private:
	int m_server_sock;
	struct sockaddr_in m_server_addr, m_client_addr;
	socklen_t m_client_addr_len;
	epoll_event m_epoll_event; // epoll�¼�
	int m_epfd, m_event_cnt;

	char m_recvBuffer[MAX_BUFFERSIZE];
	char m_sendBuffer[MAX_BUFFERSIZE];
	// �����¼�����
	epoll_event* m_pAll_events;

	// ��ϣ��ά���׽��ֺͿͻ��˱��
	std::unordered_map<int, unsigned int> g_clientID_map; //��¼socket ��Ӧ��id��
	std::unordered_map<int, FileData> g_clientFileMap;//��¼ÿ���׽��ֵ��ļ���״̬

	unsigned int g_client_id_counter{ 0 };  // �ͻ��˱�ʶ������

	std::mutex myMutex;

	// ��������
	unsigned int assignClientId();
};


