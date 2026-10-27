#include "windowtracker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {
QPointF point(const QJsonArray& a) { return {a[0].toDouble(),a[1].toDouble()}; }
bool moved(const QRectF& a,const QRectF& b) { return a!=b; }
}

void WindowScene::PointerMotion::expire(qint64 now) {
    while (!pending.empty() && (now-pending.front().time>200 || pending.size()>16)) pending.pop_front();
}
bool WindowScene::PointerMotion::consume(QPointF delta,double tolerance) {
    // Match a displacement along recent, not-yet-consumed pointer motion.
    // A title-bar gesture can consume an initial drag threshold before moving
    // the window, or deliver the move after the pointer has already stopped.
    double bestError=tolerance+1;
    size_t bestEnd=0; double bestFraction=0; bool found=false;
    for (size_t start=0;start<pending.size();++start) {
        QPointF prefix;
        for (size_t end=start;end<pending.size();++end) {
            const auto step=pending[end].delta;
            const double lengthSquared=QPointF::dotProduct(step,step);
            if (lengthSquared<=.01) continue;
            const double fraction=std::clamp(QPointF::dotProduct(delta-prefix,step)/lengthSquared,0.0,1.0);
            const QPointF matched=prefix+step*fraction;
            const double error=std::hypot(delta.x()-matched.x(),delta.y()-matched.y());
            if (QPointF::dotProduct(delta,matched)>0 && error<=tolerance && error<bestError) {
                bestError=error;bestEnd=end;bestFraction=fraction;found=true;
            }
            prefix+=step;
        }
    }
    if (!found) return false;
    for (size_t i=0;i<bestEnd;++i) pending.pop_front();
    pending.front().delta*=1-bestFraction;
    if (std::hypot(pending.front().delta.x(),pending.front().delta.y())<.1) pending.pop_front();
    return true;
}

void WindowScene::clear() {
    m_windows.clear(); m_monitors.clear(); m_suspensions.clear(); m_pointerMotion.clear(); m_previousTime=-1; ++m_generation;
}
void WindowScene::suspend(qint64 now) {
    m_blockedUntil=std::max(m_blockedUntil,now+m_settleMs); ++m_generation;
}
void WindowScene::suspendWindow(const QString& id,qint64 now) {
    if (id.isEmpty()) return;
    auto& state=m_suspensions[id];
    state.until=std::max(state.until,now+m_settleMs);
    state.revision=++m_nextRevision;
}
void WindowScene::setAnimations(const QJsonArray& animations) {
    // Hyprland reports unoverridden children as speed=0. Taking the longest
    // relevant ancestor/override covers inherited window/workspace animations.
    double longest=0;
    for (const auto& value:(animations.isEmpty()?QJsonArray():animations[0].toArray())) {
        const auto a=value.toObject(); const QString name=a["name"].toString();
        if (a["enabled"].toBool() && (name=="global" || name.startsWith("windows") ||
            name.startsWith("workspaces") || name.startsWith("specialWorkspace") ||
            name=="fade" || name=="fadeIn" || name=="fadeOut"))
            longest=std::max(longest,a["speed"].toDouble()*100);
    }
    m_settleMs=static_cast<int>(std::clamp(longest+150,200.0,60000.0));
}
void WindowScene::update(const QJsonArray& clients,const QJsonArray& monitors,QPointF cursor,
                         qint64 now,bool trackMoving) {
    QMap<QString,Monitor> nextMonitors;
    QMap<int,Monitor> byId;
    QSet<int> changedGeometry,changedWorkspaces;
    for (const auto& value:monitors) {
        const auto m=value.toObject(); const auto name=m["name"].toString();
        double w=m["width"].toDouble(),h=m["height"].toDouble(),scale=m["scale"].toDouble(1);
        if (scale<=0 || w<=0 || h<=0 || m["disabled"].toBool()) continue;
        if (m["transform"].toInt()%2) std::swap(w,h);
        Monitor mon{{m["x"].toDouble(),m["y"].toDouble(),w/scale,h/scale},
                    m["activeWorkspace"].toObject()["id"].toInt(),m["specialWorkspace"].toObject()["id"].toInt()};
        nextMonitors[name]=mon; byId[m["id"].toInt()]=mon;
        if (!m_monitors.contains(name) || m_monitors[name].rect!=mon.rect) changedGeometry.insert(m["id"].toInt());
        if (!m_monitors.contains(name) || m_monitors[name].workspace!=mon.workspace || m_monitors[name].special!=mon.special)
            changedWorkspaces.insert(m["id"].toInt());
    }
    QMap<QString,Window> next;
    int order=0;
    for (const auto& value:clients) {
        const auto c=value.toObject(); const auto at=c["at"].toArray(),size=c["size"].toArray();
        if (!c["mapped"].toBool() || c["hidden"].toBool() || (c.contains("visible") && !c["visible"].toBool()) ||
            at.size()!=2 || size.size()!=2) continue;
        Window w; w.rect={point(at),QSizeF(size[0].toDouble(),size[1].toDouble())};
        w.workspace=c["workspace"].toObject()["id"].toInt(); w.monitor=c["monitor"].toInt(-1);
        w.fullscreen=c["fullscreen"].toInt(); w.pinned=c["pinned"].toBool(); w.floating=c["floating"].toBool(); w.order=order++;
        const auto monitor=byId.constFind(w.monitor);
        if (!w.rect.isValid() || monitor==byId.cend()) continue;
        if (!w.pinned && w.workspace!=(monitor->special?monitor->special:monitor->workspace)) continue;
        const QString id=c["address"].toString();
        if (!id.isEmpty()) next[id]=w;
    }
    const QPointF pointerDelta=cursor-m_cursor;
    for (auto i=next.cbegin();i!=next.cend();++i) {
        auto previous=m_windows.constFind(i.key());
        if (previous==m_windows.cend()) {
            m_pointerMotion.remove(i.key());
            if (m_previousTime>=0) suspendWindow(i.key(),now);
            continue;
        }
        const auto& old=previous.value(); const auto& w=i.value();
        const bool metadataChanged=old.workspace!=w.workspace || old.monitor!=w.monitor ||
            old.fullscreen!=w.fullscreen || old.floating!=w.floating || old.pinned!=w.pinned ||
            changedGeometry.contains(w.monitor) || (!w.pinned && changedWorkspaces.contains(w.monitor));
        auto& motion=m_pointerMotion[i.key()];motion.expire(now);
        const bool eligible=trackMoving && !metadataChanged && w.floating && old.rect.size()==w.rect.size() &&
            m_previousTime>=0 && now-m_previousTime<=250 &&
            old.rect.adjusted(-16,-16,16,16).contains(m_cursor) && w.rect.adjusted(-16,-16,16,16).contains(cursor);
        if (!eligible) motion.pending.clear();
        else if (std::hypot(pointerDelta.x(),pointerDelta.y())>.1) motion.pending.push_back({pointerDelta,now});
        if (!moved(old.rect,w.rect) && !metadataChanged) continue;
        const QPointF delta=w.rect.topLeft()-old.rect.topLeft();
        const double distance=std::hypot(delta.x(),delta.y());
        // No per-app exceptions: both compositor and client-initiated drags
        // must have fresh matching pointer movement. Never reuse consumed motion
        // to excuse a subsequent keyboard jump or an unbounded animation.
        constexpr double maximumDragStep=240; // 1.5 times the original 160 px.
        const bool manual=eligible && distance>.1 && distance<=maximumDragStep &&
            motion.consume(delta,std::clamp(distance*.12,2.0,16.0));
        if (!manual) {suspendWindow(i.key(),now);motion.pending.clear();}
    }
    for (auto i=m_suspensions.begin();i!=m_suspensions.end();) {
        if (!next.contains(i.key()) && i->until<=now) i=m_suspensions.erase(i); else ++i;
    }
    for (auto i=m_pointerMotion.begin();i!=m_pointerMotion.end();) {
        if (!next.contains(i.key())) i=m_pointerMotion.erase(i); else ++i;
    }
    m_windows=std::move(next); m_monitors=std::move(nextMonitors); m_cursor=cursor; m_previousTime=now;
}
std::vector<StandingEdge> WindowScene::edges(const QString& monitor,qint64 now) const {
    std::vector<StandingEdge> result;
    auto mon=m_monitors.constFind(monitor);
    if (now<m_blockedUntil || mon==m_monitors.cend()) return result;
    const QRectF screen=mon->rect;
    for (auto i=m_windows.cbegin();i!=m_windows.cend();++i) {
        const auto& w=i.value();
        if (w.fullscreen || now<m_suspensions.value(i.key()).until) continue;
        for (bool bottom:{false,true}) {
            const double y=bottom?w.rect.bottom():w.rect.top();
            if (y<screen.top() || y>screen.bottom()) continue;
            std::vector<std::pair<double,double>> spans{{std::max(screen.left(),w.rect.left()),std::min(screen.right(),w.rect.right())}};
            for (auto j=m_windows.cbegin();j!=m_windows.cend();++j) {
                if (i==j) continue;
                // A moving window's IPC rectangle is its animation target, so
                // it must not hide stable supports while detection is paused.
                if (now<m_suspensions.value(j.key()).until) continue;
                const auto& other=j.value();
                // clients follows Hyprland's window stack. Floating windows
                // render above tiled windows, then stack order within each.
                const bool above=other.fullscreen || (other.floating!=w.floating?other.floating:other.order>w.order);
                if (!above || y<other.rect.top() || y>other.rect.bottom()) continue;
                std::vector<std::pair<double,double>> clipped;
                for (auto [left,right]:spans) {
                    if (other.rect.right()<=left || other.rect.left()>=right) clipped.emplace_back(left,right);
                    else {
                        if (other.rect.left()>left) clipped.emplace_back(left,other.rect.left());
                        if (other.rect.right()<right) clipped.emplace_back(other.rect.right(),right);
                    }
                }
                spans=std::move(clipped);
            }
            for (auto [left,right]:spans) if (right>left)
                result.push_back({(i.key()+':'+QString::number(m_suspensions.value(i.key()).revision)+(bottom?":bottom":":top")).toStdString(),left-screen.left(),right-screen.left(),
                                  y-screen.top(),w.rect.left()-screen.left()});
        }
    }
    return result;
}

WindowTracker::WindowTracker(QObject* parent):QObject(parent) {
    m_clock.start(); m_poll.setInterval(50); m_timeout.setSingleShot(true); m_timeout.setInterval(300);
    connect(&m_poll,&QTimer::timeout,this,&WindowTracker::poll);
    connect(&m_timeout,&QTimer::timeout,this,&WindowTracker::failed);
    connect(&m_socket,&QLocalSocket::connected,this,[this] {
        // Hyprland executes this batch synchronously: window geometry, monitor
        // state and pointer coordinates belong to the same compositor snapshot.
        static const char* commands[]={"j/animations","[[BATCH]]j/clients;j/monitors;j/cursorpos"};
        m_socket.write(commands[m_request]);
        // Hyprland waits for request bytes on its Wayland event thread. Leaving
        // Qt's write buffered until the next GUI event can deadlock a concurrent
        // EGL/Wayland round trip during window creation or resize (5s timeout).
        // Flush this tiny command before returning; never wait for the reply.
        m_socket.flush();
        if (m_socket.bytesToWrite()) failed();
    });
    connect(&m_socket,&QLocalSocket::readyRead,this,[this] {
        m_reply+=m_socket.readAll(); if (m_reply.size()>2*1024*1024) failed();
    });
    connect(&m_socket,&QLocalSocket::disconnected,this,&WindowTracker::received);
    connect(&m_socket,&QLocalSocket::errorOccurred,this,[this](QLocalSocket::LocalSocketError e) {
        if (e!=QLocalSocket::PeerClosedError) failed();
    });
    connect(&m_events,&QLocalSocket::readyRead,this,&WindowTracker::readEvents);
}
QString WindowTracker::socketPath(const QString& filename) const {
    const auto runtime=qEnvironmentVariable("XDG_RUNTIME_DIR"),signature=qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    return runtime.isEmpty() || signature.isEmpty()?QString():runtime+"/hypr/"+signature+'/'+filename;
}
void WindowTracker::configure(bool enabled,bool trackMoving) {
    m_trackMoving=trackMoving;
    if (enabled==m_enabled) return;
    m_enabled=enabled;
    if (enabled) { m_needConfig=true; m_poll.start(); poll(); }
    else {
        m_poll.stop(); m_timeout.stop(); m_inFlight=false; m_socket.abort(); m_events.abort();
        m_eventBuffer.clear(); m_scene.clear(); emit updated();
    }
}
void WindowTracker::poll() {
    if (!m_enabled || m_inFlight) return;
    if (socketPath(".socket.sock").isEmpty()) { failed(); return; }
    if (m_events.state()==QLocalSocket::UnconnectedState) m_events.connectToServer(socketPath(".socket2.sock"));
    request(m_needConfig?Animations:Snapshot);
}
void WindowTracker::request(Request kind) {
    m_request=kind; m_reply.clear(); m_inFlight=true; m_timeout.start();
    // Finish the previous socket's disconnect notification before reusing it.
    QTimer::singleShot(0,this,[this,kind] {
        if (m_enabled && m_inFlight && m_request==kind) m_socket.connectToServer(socketPath(".socket.sock"));
    });
}
void WindowTracker::received() {
    if (!m_inFlight || !m_enabled) return;
    if (m_socket.bytesAvailable()) m_reply+=m_socket.readAll();
    m_timeout.stop(); m_inFlight=false;
    if (m_request==Animations) {
        QJsonParseError error;const auto document=QJsonDocument::fromJson(m_reply,&error);
        if (error.error==QJsonParseError::NoError && document.isArray()) m_scene.setAnimations(document.array());
        m_needConfig=false;request(Snapshot);return;
    }
    QList<QJsonDocument> parts;
    qsizetype offset=0;
    while (offset<m_reply.size()) {
        const auto end=m_reply.indexOf("\n\n\n",offset);
        QJsonParseError error;
        parts.append(QJsonDocument::fromJson(end<0?m_reply.mid(offset):m_reply.mid(offset,end-offset),&error));
        if (error.error!=QJsonParseError::NoError || parts.size()>3) {failed();return;}
        if (end<0) break;
        offset=end+3;
    }
    if (parts.size()!=3 || !parts[0].isArray() || !parts[1].isArray() || !parts[2].isObject()) {failed();return;}
    const auto cursor=parts[2].object();
    if (!cursor["x"].isDouble() || !cursor["y"].isDouble()) {failed();return;}
    m_scene.update(parts[0].array(),parts[1].array(),{cursor["x"].toDouble(),cursor["y"].toDouble()},m_clock.elapsed(),m_trackMoving);
    emit updated();
}
void WindowTracker::failed() {
    m_inFlight=false; m_timeout.stop(); m_socket.abort(); m_scene.clear();
    m_scene.suspend(m_clock.elapsed()); emit updated();
}
void WindowTracker::readEvents() {
    m_eventBuffer+=m_events.readAll();
    if (m_eventBuffer.size()>1024*1024) { m_eventBuffer.clear(); return; }
    while (m_eventBuffer.contains('\n')) {
        const auto end=m_eventBuffer.indexOf('\n'); const auto line=m_eventBuffer.left(end);
        m_eventBuffer.remove(0,end+1);
        const auto separator=line.indexOf(">>");
        if (separator<0) continue;
        const auto event=line.left(separator),data=line.mid(separator+2);
        if (event=="configreloaded") m_needConfig=true;
        if (event=="openwindow" || event=="closewindow" || event=="movewindow" || event=="changefloatingmode") {
            QString address=QString::fromUtf8(data.split(',').first()).trimmed().toLower();
            if (!address.isEmpty()) {
                if (!address.startsWith("0x")) address.prepend("0x");
                m_scene.suspendWindow(address,m_clock.elapsed()); emit updated();
            }
        }
        if (event=="workspace" || event=="activespecial" || event=="fullscreen" || event=="configreloaded" ||
            event=="monitoradded" || event=="monitorremoved") {
            // These events lack a window address. Let the next snapshot identify
            // affected windows instead of suspending unrelated outputs/pets.
            poll();
        }
    }
}
QByteArray WindowTracker::frame(const QString& monitor) const {
    const auto edges=m_scene.edges(monitor,m_clock.elapsed());
    std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(12);
    out<<"ARKPETS_EDGES "<<m_scene.generation()<<' '<<edges.size();
    for (const auto& e:edges) out<<' '<<std::quoted(e.id)<<' '<<e.left<<' '<<e.right<<' '<<e.y<<' '<<e.originX;
    out<<'\n'; return QByteArray::fromStdString(out.str());
}
