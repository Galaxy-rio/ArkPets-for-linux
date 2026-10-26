#pragma once
#include <QQuickItem>
#include <QWheelEvent>

// A viewport sibling of the delegates, not part of the moving content item.
// Only wheel events are accepted: card clicks and scrollbar grabs pass through.
class ListWheelHandler : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QQuickItem* target MEMBER m_target NOTIFY targetChanged)
    Q_PROPERTY(qreal step MEMBER m_step NOTIFY stepChanged)
public:
    explicit ListWheelHandler(QQuickItem* parent = nullptr) : QQuickItem(parent) {
        setAcceptedMouseButtons(Qt::NoButton);
    }
signals:
    void targetChanged();
    void stepChanged();
protected:
    void wheelEvent(QWheelEvent* event) override;
private:
    QQuickItem* m_target = nullptr;
    qreal m_step = 88;
};
