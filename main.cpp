#include <cstdio>
#include "TCPServer.h"

int main()
{
    puts("服务器正在启动");
	TCPServer server;
	server.run();
	//TCPServer server;
	//std::string fileList = server.getFileList("/root/projects/QQServer/bin/x64/Debug/myFiles");
	//std::cout << "File List:\n" << fileList << std::endl;
    return 0;
}