#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tcpclient.h"
#include <thread>
#include <QTimer>

//#define SERVER_IP_ADDR "47.108.161.172"
#define SERVER_IP_ADDR "47.108.161.172"
#define SERVER_PORT 2233


std::mutex mutex1;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_pTimer_updateUi = new QTimer(this);
    m_pTimer_updateUi->setInterval(500);//5毫秒

    initUi();
    //信号和槽
    connect(m_pTimer_updateUi,&QTimer::timeout,this,&MainWindow::slot_timeout_update);
    connect(m_textEdit,&ChatTextEdit::enterPressed,ui->pb_send,&QPushButton::click);
    std::thread t(std::bind(&MainWindow::startThread_client, this));
    t.detach();

    m_pTimer_updateUi->start();

}

MainWindow::~MainWindow()
{
    delete ui;
    if(client != nullptr){
        client->sendMsg("code:0");//下线
        client->stopReceiving();
        delete client;
    }
}

void MainWindow::initUi()
{
    // 获取原来的 QTextEdit 的几何属性
    QRect originalGeometry = ui->textEdit->geometry();
    // 删除原来的 QTextEdit 组件
    delete ui->textEdit;
    ui->textEdit = nullptr;
    // 创建新的 ChatTextEdit 实例
    m_textEdit = new ChatTextEdit(this);  // 确保 this 是一个有效的 QWidget
    // 设置新的 QTextEdit 的位置和尺寸
    m_textEdit->setGeometry(originalGeometry);
    m_textEdit->setText("");

    //初始化聊天视图 绑定model
    m_Model_chatHistory = new ReadOnlyStringListModel(this);
    ui->lv_chatRoom->setModel(m_Model_chatHistory);

    //初始化用户界面
    m_Model_user = new ReadOnlyStringListModel(this);
    ui->lv_user->setModel(m_Model_user);

    //初始化文件列表界面
    m_Model_fileList = new ReadOnlyStringListModel(this);
    ui->lv_file->setModel(m_Model_fileList);

    //初始化状态栏
    m_statusLabel = new QLabel("未连接到服务器",this);
    ui->statusbar->addWidget(m_statusLabel);
}

void MainWindow::startThread_client()
{
    client = new TCPClient(SERVER_IP_ADDR,SERVER_PORT);
    client->startReceiving();
    client->sendMsg("code:1");//上线

    std::lock_guard<std::mutex> lockGuard(mutex1);
    m_Model_fileList->haveNewMsg = true;
}


/**
 * @brief MainWindow::on_pushButton_5_clicked
 * @todo 点击按钮 将消息放入缓冲区发送至服务器
 */
void MainWindow::on_pb_send_clicked()
{
    if((client == nullptr) || (m_textEdit->toPlainText().length() == 0)) return;
    //发送完就清空输入框
    qDebug()<<m_textEdit->toPlainText();
    client->sendMsg(m_textEdit->toPlainText());
    m_textEdit->clear();
    //创建一个信号 作为线程间通信
}

/**
 * @brief MainWindow::slot_timeout_update
 * 更新 界面
 */
void MainWindow::slot_timeout_update()
{
    if(client == nullptr) return;
    if(client->isConnected) m_statusLabel->setText("连接服务器成功");
    else m_statusLabel->setText("未连接到服务器");
    curTime = QTime::currentTime();
    QString timeString = curTime.toString("HH:mm:ss");
    ui->lb_curTime->setText(timeString);

    m_Model_chatHistory->setStringList(client->getMessageList());

    m_Model_user->setStringList(client->getUserList());

    ui->lb_onlineNums->setText(QString::number(client->getUserList().size()));

    // if(m_Model_fileList->haveNewMsg){
    //     m_Model_fileList->setStringList(client->getFileList());
    //     if(client->temp_filelist != client->getFileList())
    //         m_Model_fileList->haveNewMsg = false;
    // }
    {
        std::lock_guard<std::mutex> lock(TCPClient::mtx_sendFileStatus);
        if(ReadOnlyStringListModel::haveNewMsg){
            m_Model_fileList->setStringList(client->getFileList());
            if(client->temp_filelist != client->getFileList())
                ReadOnlyStringListModel::haveNewMsg = false;
        }
    }

}

/*清空聊天列表*/
void MainWindow::on_pb_clearChatInfo_clicked()
{
}
QString MainWindow::formatFileSize(qint64 size) {
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

void MainWindow::on_pb_updateFile_clicked()
{
    if(client == nullptr || (!client->isConnected)) return;

    // 选择文件并获取文件路径
    QString filePath = QFileDialog::getOpenFileName(this, "选择文件");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "无法打开文件:" << file.errorString();
        return;
    }

    // 发送文件信息
    QString fileName = QFileInfo(filePath).fileName();
    qint64 fileSize = file.size();
    QString header = QString("UPLOAD,%1,%2").arg(fileName).arg(fileSize);
    client->sendMsg(header);

    // 创建进度对话框
    QProgressDialog progressDialog("正在上传文件...", "取消", 0, fileSize, this);
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setValue(0);
    progressDialog.show();

    // 缓冲区设置为64KB
    char buffer[1024 * 64];
    qint64 totalSent = 0;

    // 按块读取文件并发送
    while (!file.atEnd() && totalSent < fileSize) {
        qint64 bytesRead = file.read(buffer, sizeof(buffer));
        if (bytesRead == -1) {
            qDebug() << "读取文件时出错:" << file.errorString();
            break;
        }

        qint64 bytesToSend = bytesRead;
        qint64 bytesSent = 0;

        while (bytesToSend > 0) {
            qint64 ret_send = send(client->getSocket(), buffer + bytesSent, bytesToSend, 0);
            if (ret_send == SOCKET_ERROR) {
                qDebug() << "发送失败，错误代码:" << WSAGetLastError();
                return;
            }

            bytesSent += ret_send;
            bytesToSend -= ret_send;
            totalSent += ret_send;

            // 更新进度条
            progressDialog.setValue(static_cast<int>(totalSent));

            if (progressDialog.wasCanceled()) {
                qDebug() << "上传取消";
                return;
            }
        }
    }

    // 关闭文件
    file.close();

    progressDialog.setValue(fileSize); // 进度条完成
    qDebug() << "文件上传完成";
    client->temp_filelist = client->getFileList();
    {
        std::lock_guard<std::mutex> lock(TCPClient::mtx_sendFileStatus);
        ReadOnlyStringListModel::haveNewMsg = true;
    }
}

void MainWindow::on_pb_downloadFile_clicked()
{
    // 确保客户端已连接
    if (client == nullptr || (!client->isConnected)) return;

    // 获取当前选中的文件
    QModelIndex currentIndex = ui->lv_file->currentIndex();
    if (!currentIndex.isValid()) return;

    QString selectedItem = currentIndex.data().toString();
    QString fileName = selectedItem.split("\n").at(1);

    // 弹出文件保存对话框
    QString savePath = QFileDialog::getSaveFileName(this, "Save File", fileName);
    if (savePath.isEmpty()) return;

    qDebug() << "Current selected item:" << fileName;
    qDebug() << "Saving file to:" << savePath;

    // 保护 b_sendFileStatus 的修改
    {
        std::lock_guard<std::mutex> lock(TCPClient::mtx_sendFileStatus);
        TCPClient::b_sendFileStatus = true;
    }
    qDebug()<<"qDebug()<<b_sendFileStatus:"<<TCPClient::b_sendFileStatus;

    // 发送下载请求给服务器
    TCPClient::g_Ori_FileSize = TCPClient::g_fileSize;
    client->sendMsg(QString("<download>:%1").arg(fileName));
    while(TCPClient::b_sendFileStatus == false);

    qDebug()<<"client->sendMsg(QString(\"<download>:%1\").arg(fileName))";

    // 等待接收文件大小（假设服务器首先返回文件大小）
    while (TCPClient::g_fileSize == TCPClient::g_Ori_FileSize); // 做数据同步


    qDebug() << "g_client_filesize:" << TCPClient::g_fileSize; // 拿到文件大小
    qint64 fileSize = TCPClient::g_fileSize;

    // 创建进度对话框
    QProgressDialog progressDialog("正在下载文件...", "取消", 0, fileSize, this);//局部自动释放关闭
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setValue(0);
    progressDialog.show();

    char buffer[1024 * 64] = {0}; // 64KB 缓冲区
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open file for writing.";
        return;
    }

    // 接收文件数据并写入到文件中
    qint64 totalBytesReceived = 0;
    while (totalBytesReceived < fileSize) {
        int len_recv = recv(client->getSocket(), buffer, sizeof(buffer), 0);
        if (len_recv <= 0) break;

        totalBytesReceived += len_recv;
        file.write(buffer, len_recv);

        // 更新进度条
        progressDialog.setValue(static_cast<int>(totalBytesReceived));

        if (progressDialog.wasCanceled()) {
            qDebug() << "下载取消";
            file.close();
            return;
        }
    }

    file.close();
    qDebug() << "File download completed.";

    {
        std::lock_guard<std::mutex> lock(TCPClient::mtx_sendFileStatus);
        TCPClient::b_sendFileStatus = false;
    }

    progressDialog.setValue(fileSize); // 进度条完成
}

