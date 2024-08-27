#include "chattextedit.h"
#include <QKeyEvent>

ChatTextEdit::ChatTextEdit(QWidget *parent)
{
    this->setParent(parent);
}

void ChatTextEdit::keyPressEvent(QKeyEvent *event)
{
    // 检查是否按下回车键
    if (event->key() == Qt::Key_Return) {
        if (event->modifiers() == Qt::ShiftModifier) {
            // Shift+回车: 插入换行符
            ChatTextEdit::insertPlainText("\n");
        } else {
            // 只按回车: 触发自定义信号
            emit enterPressed();
        }
        return;  // 阻止默认的回车行为
    }

    // 对于其他按键，保持默认处理
    QTextEdit::keyPressEvent(event);

}
