#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <iostream>
#include <stdio.h>
#include <process.h>
#include <windows.h>
#include <QString>
#include <QStringList>
#include <thread>
#include <mutex>
#include <QByteArray>


#pragma comment(lib,"ws2_32.lib")

class TCPClient
{
public:
    TCPClient(QString ip_addr,SHORT server_port);
    ~TCPClient();
    int initClient();

    void startReceiving();
    void stopReceiving();
    std::thread recvThread;
    std::atomic<bool> running;

    size_t sendMsg(QString Msg);
    //size_t sendFile(const QByteArray& data) const;
    inline static std::mutex mtx_sendFileStatus;

    void recvMsg();
    void recvFile();

    inline static bool isConnected{false};
    QString curMsg{"NULL"};

    QStringList getMessageList() const;  // 用于其他类访问消息列表
    QStringList getUserList() const;

    SOCKET getSocket() const;

    QStringList getFileList() const;
    QStringList temp_filelist;

    QStringList getFileNamesList() const;
    inline static bool b_sendFileStatus = false;
    inline static qint64 g_fileSize{-1};
    inline static qint64 g_Ori_FileSize{g_fileSize};
protected:
    void handleRecvMsg(QString& msg);
    QStringList parseFileInfo(const QString &data);
    QString formatFileSize(qint64 size);

private:
    SOCKADDR_IN m_addr_server;
    SOCKET m_Socket;
    char m_SendBuffer[1024];
    char m_RecvBuffer[1024];
    QStringList m_messageList;  // 用于存储接收到的消息
    QStringList m_userList;  // 用于存储接收到的消息
    QStringList m_fileList;
    QStringList m_fileNamesList;

};

#endif // TCPCLIENT_H
