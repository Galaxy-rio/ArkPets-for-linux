#pragma once

#include "../gravity.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLocalSocket>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QTimer>
#include <deque>

// Deterministic scene/motion policy, separate from the asynchronous IPC client.
class WindowScene {
public:
    void update(const QJsonArray& clients, const QJsonArray& monitors, QPointF cursor,
                qint64 now, bool trackMoving);
    void suspend(qint64 now);
    void suspendWindow(const QString& id, qint64 now);
    void clear();
    void setAnimations(const QJsonArray& animations);
    std::vector<StandingEdge> edges(const QString& monitor, qint64 now) const;
    unsigned long long generation() const { return m_generation; }
private:
    struct Window {
        QRectF rect;
        int workspace = 0, monitor = -1, fullscreen = 0, order = 0;
        bool floating = false, pinned = false;
    };
    struct Monitor { QRectF rect; int workspace = 0, special = 0; };
    QMap<QString,Window> m_windows;
    QMap<QString,Monitor> m_monitors;
    struct Suspension { qint64 until = 0; unsigned long long revision = 0; };
    QMap<QString,Suspension> m_suspensions;
    struct PointerMotion {
        struct Step { QPointF delta; qint64 time; };
        std::deque<Step> pending;
        void expire(qint64 now);
        bool consume(QPointF delta, double tolerance);
    };
    QMap<QString,PointerMotion> m_pointerMotion;
    unsigned long long m_nextRevision = 0;
    QPointF m_cursor;
    qint64 m_previousTime = -1, m_blockedUntil = 0;
    int m_settleMs = 1000;
    unsigned long long m_generation = 0;
};

class WindowTracker : public QObject {
    Q_OBJECT
public:
    explicit WindowTracker(QObject* parent = nullptr);
    void configure(bool enabled, bool trackMoving);
    QByteArray frame(const QString& monitor) const;
signals:
    void updated();
private:
    WindowScene m_scene;
    QElapsedTimer m_clock;
    QTimer m_poll, m_timeout;
    QLocalSocket m_socket, m_events;
    QByteArray m_reply, m_eventBuffer;
    bool m_enabled = false, m_trackMoving = false, m_needConfig = true, m_inFlight = false;
    enum Request { Animations, Snapshot } m_request = Snapshot;
    void poll();
    void request(Request kind);
    void received();
    void failed();
    void readEvents();
    QString socketPath(const QString& filename) const;
};
