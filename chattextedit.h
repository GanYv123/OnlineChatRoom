#ifndef CHATTEXTEDIT_H
#define CHATTEXTEDIT_H

#include <QObject>
#include <QTextEdit>
#include <QWidget>

class ChatTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    ChatTextEdit(QWidget* parent=nullptr);

signals:
    void enterPressed();  // 自定义信号，用于触发发送按钮
protected:
    void keyPressEvent(QKeyEvent* event) override;
};

#endif // CHATTEXTEDIT_H
