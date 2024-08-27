#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QShortcut>
#include "chattextedit.h"
#include "tcpclient.h"
#include <QLabel>
#include <QTimer>
#include <QTime>
#include <QStringListModel>
#include <QFileDialog>
#include <QFile>
#include <QByteArray>
#include <QProgressDialog>
#include <QModelIndex>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ReadOnlyStringListModel : public QStringListModel {
    Q_OBJECT
public:
    explicit ReadOnlyStringListModel(QObject *parent = nullptr)
        : QStringListModel(parent) {}

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        Qt::ItemFlags flags = QStringListModel::flags(index);
        flags &= ~Qt::ItemIsEditable; //清除可编辑选项
        return flags;
    }
    inline static bool haveNewMsg{false};
};


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();


protected:
    void initUi();
    void startThread_client();
    //创建快捷方式
    QShortcut* shortCut_sendMsg;
    QString formatFileSize(qint64 size);

signals:

private slots:
    void on_pb_send_clicked();
    void slot_timeout_update();

    void on_pb_clearChatInfo_clicked();
    void on_pb_updateFile_clicked();

    void on_pb_downloadFile_clicked();

private:
    Ui::MainWindow *ui;
    ChatTextEdit *m_textEdit;
    QLabel *m_statusLabel;
    TCPClient* client;
    //定时器
    QTimer* m_pTimer_updateUi;

    QTime curTime;
    ReadOnlyStringListModel* m_Model_chatHistory; //聊天记录模型
    ReadOnlyStringListModel* m_Model_user; //用户模型
    ReadOnlyStringListModel* m_Model_fileList;//文件列表模型
    QStringList currentAllUser; //用户id
    QStringList fileList;

};

#endif // MAINWINDOW_H
