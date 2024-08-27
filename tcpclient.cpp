#include "tcpclient.h"
#include "QMessageBox"
#include <string>
#include <QDebug>
#include <cstring>
#include <cstdlib>
#include <QTime>
#include "mainwindow.h"

#undef connect  // 取消 connect 宏定义

int TCPClient::initClient()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    // 请求使用的 Winsock 版本为 2.2
    wVersionRequested = MAKEWORD(2, 2);

    // 初始化 Winsock 库
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        // 如果初始化失败，显示错误信息并返回错误码
        qDebug() << "WSAStartup failed with error:" << err;
        return err;
    }

    // 检查 Winsock 库是否支持 2.2 版本
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        // 如果版本不匹配，显示错误信息并清理 Winsock 库
        qDebug() << "Could not find a usable version of Winsock.dll";
        WSACleanup();
        return -1;
    }

    // 初始化成功，显示成功信息
    qDebug() << "Winsock 2.2 initialized successfully.";
    return 0;
}

TCPClient::TCPClient(QString ip_addr, SHORT server_port)
{
    int ret = initClient();
    if (ret != 0) {
        return;
    }

    m_addr_server.sin_addr.S_un.S_addr = inet_addr(ip_addr.toStdString().c_str());
    //m_addr_server.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
    m_addr_server.sin_family = AF_INET;
    m_addr_server.sin_port = htons(server_port);
    //m_addr_server.sin_port = htons(2233);

    m_Socket = socket(PF_INET, SOCK_STREAM, 0);

    if (INVALID_SOCKET == m_Socket) {
        qDebug() << "socket error:" << WSAGetLastError();
        return;
    }

    qDebug() << "Attempting to connect...";
    int connectResult = connect(m_Socket, (SOCKADDR*)&m_addr_server, sizeof(m_addr_server));
    if (SOCKET_ERROR == connectResult) {
        qDebug() << "connect error:" << WSAGetLastError();
        return;
    }

    qDebug() << "connect successful";
    //sendMsg("首次连接");
    isConnected = true;
}

TCPClient::~TCPClient()
{
    isConnected = false;
}

void TCPClient::recvMsg() {
    while (running&&isConnected) {
        //如果传输状态就不走这个recv
        {
            std::lock_guard<std::mutex> lock(TCPClient::mtx_sendFileStatus);
            if (TCPClient::b_sendFileStatus == true){
                //qDebug()<<__FUNCTION__<<"-------T R U E-------";
                continue;
            }
            //qDebug()<<__FUNCTION__<<"----<<"<<TCPClient::b_sendFileStatus;
        }

        memset(m_RecvBuffer, 0, sizeof(m_RecvBuffer));
        size_t ret_recv = recv(m_Socket, m_RecvBuffer, sizeof(m_RecvBuffer), 0);
        if (ret_recv > 0) {
            qDebug() << "接收到的消息:" << m_RecvBuffer;
            curMsg = QString(m_RecvBuffer); //最后一条消息
            if(curMsg.startsWith("fileSize:")){
                std::lock_guard<std::mutex> lock(TCPClient::mtx_sendFileStatus);
                TCPClient::b_sendFileStatus = true;
            }
            handleRecvMsg(curMsg);

        } else if (ret_recv == 0) {
            //断开连接

            break;
        } else {
            // Error handling
            qDebug() << "接收失败，错误代码:" << errno;

            break;
        }
    }
}

QStringList TCPClient::getMessageList() const
{
    // 调试信息：输出 m_messageList 的大小和内容
    //qDebug() << "getMessageList called, size:" << m_messageList.size();
    // for (const QString &message : m_messageList) {
    //     qDebug() << "Message:" << message;
    // }
    return m_messageList;
}

void TCPClient::handleRecvMsg(QString& msg)
{
    if(msg.startsWith("IDs:")&&msg.endsWith(",")){
        //拿到在线用户消息
        QString trimmedMsg = msg.chopped(1); // 去掉末尾的一个字符
        QString idsPart = trimmedMsg.mid(4); // 从第4个字符开始截取
        // 按逗号分隔
        m_userList = idsPart.split(',');
        // 打印结果以验证
        qDebug() << "IDs List:" << m_userList;
    }
    else if(msg.startsWith("FileInfo>>") && msg.endsWith("<<FileInfoEnd")){
        //qDebug()<<"文件信息:"<<msg;
        std::lock_guard<std::mutex> lock(TCPClient::mtx_sendFileStatus);
        ReadOnlyStringListModel::haveNewMsg = true;
        m_fileList = parseFileInfo(msg);
    }else if(msg.startsWith("fileSize:")){
        TCPClient::g_fileSize = msg.split(":").at(1).toLongLong();
    }
    else{
        //普通聊天消息
        m_messageList.append(QString("\t\t\t%1\n%2")
                                 .arg(QTime::currentTime().toString("HH:mm::ss")).arg(curMsg));
            // 将接收到的消息添加到 QStringList 中
    }

}


QString TCPClient::formatFileSize(qint64 size) {
    double fileSize = size;
    QString unit;

    if (fileSize < 1024) {
        unit = "B";
    } else if (fileSize < 1024 * 1024) {
        fileSize /= 1024;
        unit = "KB";
    } else if (fileSize < 1024 * 1024 * 1024) {
        fileSize /= (1024 * 1024);
        unit = "MB";
    } else {
        fileSize /= (1024 * 1024 * 1024);
        unit = "GB";
    }

    return QString("%1 %2").arg(fileSize, 0, 'f', 2).arg(unit);
}

QStringList TCPClient::getFileNamesList() const
{
    return m_fileNamesList;
}
/**
 * @brief TCPClient::parseFileInfo
 * @param data
 * @return 处理好的stringList
 */
QStringList TCPClient::parseFileInfo(const QString &data)
{
    QStringList result;
    m_fileNamesList.clear();
    QString cleanData = data;
    cleanData.remove("FileInfo>>").remove("<<FileInfoEnd");

    QStringList fileEntries = cleanData.split("\n", Qt::SkipEmptyParts);

    for (const QString& entry : fileEntries) {
        QStringList components = entry.split(",");
        if (components.size() == 3) {
            QString formattedEntry = components[1]
                                     + "\n" + components[0]
                                     + "\n" + formatFileSize(components[2].toLongLong());
            result << formattedEntry;
            m_fileNamesList<<components[0];
        }
    }

    return result;
}

QStringList TCPClient::getFileList() const
{
    return m_fileList;
}

QStringList TCPClient::getUserList() const
{
    return m_userList;
}

SOCKET TCPClient::getSocket() const
{
    return m_Socket;
}


size_t TCPClient::sendMsg(QString Msg)
{
    if(!isConnected) return 0;
    if(Msg.length() == 0)
        return 0;
    qDebug()<<"往服务器发送:"<<Msg;
    memset(m_SendBuffer,0,sizeof(m_SendBuffer));
    strcpy(m_SendBuffer,Msg.toStdString().c_str());
    size_t ret_send = send(m_Socket,m_SendBuffer,strlen(m_SendBuffer)+1,0);
    if(ret_send > 0) qDebug()<<"发送成功"<<Msg;
    return ret_send;
}

#if 0
size_t TCPClient::sendFile(const QByteArray &data) const
{
    if (!isConnected) return 0;
    qDebug() << "发送数据...";
    size_t totalSent = 0; //目前一共发送了多少
    size_t dataSize = data.size(); // 总文件字节数

    while (totalSent < dataSize) { //分包发送，一直发直到 发送的数量 == datasize
        size_t bytesToSend = std::min(static_cast<size_t>(65536), dataSize - totalSent); // 64KB块大小
        size_t ret_send = send(m_Socket, data.data() + totalSent, bytesToSend, 0);

        if (ret_send == SOCKET_ERROR) {
            qDebug() << "发送失败，错误代码:" << WSAGetLastError();
            return totalSent;
        }

        totalSent += ret_send;
    }

    qDebug() << "发送完成";
    return totalSent;
}
#endif //0

void TCPClient::startReceiving() {
    running = true;
    recvThread = std::thread(&TCPClient::recvMsg, this);
}

void TCPClient::stopReceiving() {
    running = false;
    if (recvThread.joinable()) {
        recvThread.join();
    }
}

