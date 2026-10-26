#include "listwheelhandler.h"
#include <QDebug>

void ListWheelHandler::wheelEvent(QWheelEvent* event) {
    if (!m_target || event->buttons() != Qt::NoButton ||
        event->phase() != Qt::NoScrollPhase ||
        event->source() == Qt::MouseEventSynthesizedBySystem) {
        event->ignore(); return;
    }
    // Wayland exposes a shared seat, not the physical device. Qt can label a
    // discrete mouse wheel as TouchPad. Qt Wayland marks finger/continuous axes
    // with scroll phases and/or SynthesizedBySystem; use those event semantics.
    // Some desktop/remote input backends supply both pixel and angle deltas for
    // a mouse. Prefer the angle, but do not discard those notched wheel events.
    const qreal notches = event->angleDelta().y() != 0 ? event->angleDelta().y() / 120.0
        : event->pixelDelta().y() / 15.0;
    if (qFuzzyIsNull(notches)) {event->ignore(); return;}
    QMetaObject::invokeMethod(m_target, "cancelFlick");
    const qreal start = m_target->property("originY").toReal();
    const qreal end = start + qMax(0.0, m_target->property("contentHeight").toReal() - m_target->height());
    const qreal before = m_target->property("contentY").toReal();
    const qreal after = qBound(start, before - notches * m_step, end);
    m_target->setProperty("contentY", after);
    if (qEnvironmentVariableIsSet("ARKPETS_TRACE_SCROLL"))
        qInfo() << "ArkPets wheel" << event->angleDelta() << event->pixelDelta()
                << "phase" << event->phase() << "position" << before << "->" << after;
    event->accept();
}
