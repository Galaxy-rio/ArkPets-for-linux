#include "catalog.h"
#include "controller.h"
#include "listwheelhandler.h"
#include "stylemanager.h"
#include <QStyleHints>
#include <QSaveFile>
#include "../src/activity.h"
#include "../src/viewport.h"
#include <set>
#include <QGuiApplication>
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QLocalServer>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonArray>
#include <QJSValue>
#include <QScopeGuard>
#include <QWheelEvent>
#include <QPointingDevice>

static QByteArray readFile(const QString& path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
static QByteArray fixture() {return readFile(QString(TEST_SOURCE_DIR)+"/fixtures/catalog.json");}
static QQuickItem* visualItem(QQuickItem* root,const QString& name) {
    if(root->objectName()==name) return root;
    for(auto* child:root->childItems()) if(auto* item=visualItem(child,name)) return item;
    return nullptr;
}


class ModelServer : public QTcpServer {
public:
    QStringList requests;
    bool failImage=false, stall=false;
    QByteArray catalog=fixture();
    ModelServer() {
        connect(this,&QTcpServer::newConnection,this,[this] {
            while(auto* socket=nextPendingConnection()) {
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                auto input=std::make_shared<QByteArray>();
                connect(socket,&QTcpSocket::readyRead,this,[this,socket,input] {
                    input->append(socket->readAll());
                    if(!input->contains("\r\n\r\n"))return;
                    socket->disconnect(this);
                    QByteArray encoded=input->split(' ').value(1);
                    requests.append(QString::fromLatin1(encoded));
                    QString path=QUrl::fromEncoded("http://localhost"+encoded).path();
                    if(stall)return;
                    bool bad=failImage&&path.endsWith(".png");
                    QByteArray body;
                    if(path.endsWith("models_data.json"))body=catalog;
                    else {
                        QString extension=path.mid(path.lastIndexOf('.'));
                        body=readFile(qEnvironmentVariable("ARKPETS_TEST_MODEL_DIR")+"/build_char_2025_shu"+extension);
                        if(extension==".atlas")body.replace("build_char_2025_shu.png","build_char_2025_shu_nian#11.png");
                    }
                    socket->write(bad?"HTTP/1.1 500 Broken\r\n":"HTTP/1.1 200 OK\r\n");
                    socket->write("Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n");
                    socket->write(body);socket->disconnectFromHost();
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl base() const {return QUrl(QString("http://127.0.0.1:%1/").arg(serverPort()));}
};

class BackendTest : public QObject {
    Q_OBJECT
private slots:
    void gravityLandingFollowingAndWalkingBounds() {
        const std::vector<StandingEdge> edges{{"a:top",100,500,200,100},{"a:bottom",100,500,500,100}};
        GravityBody pet;pet.x=300;pet.y=120;pet.halfWidth=30;pet.height=100;
        pet.step(1,edges,800,600);QVERIFY(pet.onWindow());QCOMPARE(pet.y,200.0);
        QCOMPARE(pet.walkLeft(),130.0);QCOMPARE(pet.walkRight(),470.0);
        auto moved=edges;for(auto& e:moved) {e.left+=20;e.right+=20;e.originX+=20;e.y+=10;}
        pet.step(.05,moved,800,600);QCOMPARE(pet.x,320.0);QCOMPARE(pet.y,210.0);
        // Losing one support can land on a lower edge, but never pop back onto
        // the previous upper edge after a suspension between render frames.
        pet.step(.1,moved,800,600,true);QVERIFY(!pet.grounded());QVERIFY(pet.y>210);
        pet.step(1,moved,800,600);QCOMPARE(pet.y,510.0);QVERIFY(pet.onWindow());
        pet.release();pet.y=300;pet.step(1,edges,800,600);QCOMPARE(pet.y,500.0);
        pet.step(1,{},800,600);QCOMPARE(pet.y,600.0);QVERIFY(pet.grounded());QVERIFY(!pet.onWindow());
        // Subpixel rounding of layer margins on a click must not drop through.
        pet.release();pet.y=200.4;pet.step(.01,edges,800,600);QCOMPARE(pet.y,200.0);
        // A narrow ledge cannot support the body, and high-speed falling cannot tunnel.
        pet.release();pet.y=0;pet.step(1,{{"narrow",290,310,200,290}},800,600);QCOMPARE(pet.y,600.0);
        ActivityDirector walk(activityPlan({"Default","Move"},"Default"),200,17);
        for(int i=0;i<400;++i) {walk.advance(.1,470,100,130);QVERIFY(walk.right()>=130&&walk.right()<=470);}
    }
    void windowMotionPolicyAndVisibleEdges() {
        auto monitor=[](int workspace=1) {return QJsonArray{QJsonObject{{"id",0},{"name","test"},{"x",100},{"y",200},
            {"width",1600},{"height",1200},{"scale",2},{"transform",0},{"activeWorkspace",QJsonObject{{"id",workspace}}}}};};
        auto window=[](const QString& id,int x,int y,int w=400,int h=300) {return QJsonObject{{"address",id},{"mapped",true},
            {"visible",true},{"floating",true},{"monitor",0},{"workspace",QJsonObject{{"id",1}}},
            {"at",QJsonArray{x,y}},{"size",QJsonArray{w,h}}};};
        WindowScene scene;
        scene.update({window("a",200,400)},monitor(),{240,420},0,true);
        auto edges=scene.edges("test",0);QCOMPARE(edges.size(),size_t(2));
        QCOMPARE(edges[0].left,100.0);QCOMPARE(edges[0].y,200.0);QCOMPARE(edges[1].y,500.0);
        scene.update({window("a",220,410)},monitor(),{260,430},50,true);
        QCOMPARE(scene.edges("test",50).size(),size_t(2));QCOMPARE(scene.generation(),0ULL);
        scene.update({window("a",420,410)},monitor(),{260,430},100,true);
        QVERIFY(scene.edges("test",100).empty());QCOMPARE(scene.generation(),0ULL);
        QVERIFY(scene.edges("test",1000).empty());QCOMPARE(scene.edges("test",1200).size(),size_t(2));
        WindowScene disabled;
        disabled.update({window("a",200,400)},monitor(),{240,420},0,false);
        disabled.update({window("a",220,410)},monitor(),{260,430},50,false);
        QVERIFY(disabled.edges("test",50).empty());
        WindowScene resize;
        resize.update({window("a",200,400)},monitor(),{240,420},0,true);
        resize.update({window("a",200,400,420)},monitor(),{260,420},50,true);
        QVERIFY(resize.edges("test",50).empty());
        scene.update({window("a",420,410)},monitor(2),{260,430},1300,true);
        QVERIFY(scene.edges("test",2500).empty());
        WindowScene overlap;
        overlap.update({window("a",200,400),window("b",350,380,100,100)},monitor(),{0,0},0,false);
        edges=overlap.edges("test",0);
        std::vector<StandingEdge> upper;
        for(const auto& e:edges) if(e.id=="a:0:top") upper.push_back(e);
        QCOMPARE(upper.size(),size_t(2));QCOMPARE(upper[0].right,250.0);QCOMPARE(upper[1].left,350.0);
        // Suspend only the moving window, including its uncertain occlusion.
        overlap.update({window("a",200,400),window("b",355,380,100,100)},monitor(),{0,0},50,false);
        edges=overlap.edges("test",50);QCOMPARE(edges.size(),size_t(2));
        QCOMPARE(edges[0].id,std::string("a:0:top"));QCOMPARE(edges[0].right,500.0);
        // A faster pointer-correlated drag is now allowed; an excessive jump
        // or a similarly sized keyboard move still withdraws that window.
        WindowScene fast;
        fast.update({window("a",200,400)},monitor(),{240,420},0,true);
        fast.update({window("a",440,400)},monitor(),{480,420},50,true);
        QCOMPARE(fast.edges("test",50).size(),size_t(2));
        fast.update({window("a",199,400)},monitor(),{239,420},100,true);
        QVERIFY(fast.edges("test",100).empty());
        // Client title bars wait for a drag threshold: the pointer first moves
        // while the window is still stationary, then the window follows later.
        WindowScene titleBar;
        titleBar.update({window("a",200,400)},monitor(),{240,420},0,true);
        titleBar.update({window("a",200,400)},monitor(),{250,420},50,true);
        titleBar.update({window("a",208,400)},monitor(),{258,420},100,true);
        QCOMPARE(titleBar.edges("test",100).size(),size_t(2));
        titleBar.update({window("a",215,400)},monitor(),{258,420},150,true);
        QCOMPARE(titleBar.edges("test",150).size(),size_t(2));
        titleBar.update({window("a",218,400)},monitor(),{258,420},200,true);
        QCOMPARE(titleBar.edges("test",200).size(),size_t(2));
        // Consumed motion must not authorize another keyboard move, even if
        // its direction and size happen to match the previous pointer motion.
        titleBar.update({window("a",228,400)},monitor(),{258,420},250,true);
        QVERIFY(titleBar.edges("test",250).empty());
        WindowScene threshold;
        threshold.update({window("a",200,400)},monitor(),{240,420},0,true);
        threshold.update({window("a",204,400)},monitor(),{254,420},50,true);
        QCOMPARE(threshold.edges("test",50).size(),size_t(2));
        threshold.update({window("a",204,400)},monitor(),{254,420},200,true);
        threshold.update({window("a",214,400)},monitor(),{254,420},300,true);
        QVERIFY(threshold.edges("test",300).empty()); // Old hover motion expired.
        WindowScene separate;
        separate.update({window("a",200,400),window("b",700,400,200)},monitor(),{0,0},0,true);
        const auto before=separate.edges("test",0);
        GravityBody a,b;
        a.x=250;b.x=650;a.y=b.y=120;a.height=b.height=100;a.halfWidth=b.halfWidth=25;
        a.step(1,before,800,600);b.step(1,before,800,600);QVERIFY(a.onWindow()&&b.onWindow());
        separate.update({window("a",240,400),window("b",700,400,200)},monitor(),{0,0},50,true);
        edges=separate.edges("test",50);QCOMPARE(edges.size(),size_t(2));
        QCOMPARE(edges[0].id,std::string("b:0:top"));
        // Even if 1 FPS rendering misses the entire pause, only A's revised
        // support ID detaches. B keeps its footing and must not fall or jump.
        edges=separate.edges("test",1200);
        a.step(.1,edges,800,600);b.step(.1,edges,800,600);
        QVERIFY(!a.grounded());QVERIFY(a.y>200);QVERIFY(b.onWindow());QCOMPARE(b.y,200.0);
        separate.suspendWindow("a",1300);
        edges=separate.edges("test",1300);QCOMPARE(edges.size(),size_t(2));
        QCOMPARE(edges[0].id,std::string("b:0:top"));
    }
    void settingsWheelAndSwitchAlignment() {
        QTemporaryDir dir;Controller c(dir.path()+"/data",dir.path()+"/config");
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");
        engine.rootContext()->setContextProperty("backend",&c);engine.rootContext()->setContextProperty("trayAvailable",false);
        engine.setInitialProperties({{"currentPage","model-settings"},{"height",700}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* scroll=window->findChild<QQuickItem*>("generalSettingsScroll");
        auto* handler=window->findChild<QQuickItem*>("generalSettingsWheelHandler");QVERIFY(scroll&&handler);
        QTRY_VERIFY(scroll->property("contentHeight").toReal()>scroll->height()+88);
        auto* automatic=window->findChild<QQuickItem*>("autoActivitySwitch");QVERIFY(automatic);
        const auto right=automatic->mapToScene(QPointF(automatic->width(),0)).x();
        for(const auto* name:{"gravitySwitch","windowEdgesSwitch","trackMovingWindowsSwitch"}) {
            auto* item=window->findChild<QQuickItem*>(name);QVERIFY(item);
            QVERIFY(std::abs(item->mapToScene(QPointF(item->width(),0)).x()-right)<1);
        }
        QPointingDevice seat("Wayland seat",100,QInputDevice::DeviceType::TouchPad,
            QPointingDevice::PointerType::Finger,QInputDevice::Capability::Position,5,0);
        const QPointF where=scroll->mapToScene(QPointF(scroll->width()*.7,scroll->height()*.6));
        const QPointF overlayOrigin=handler->mapToScene(QPointF());
        auto wheel=[&](int angle,int pixels) {
            QWheelEvent event(where,window->mapToGlobal(where.toPoint()),QPoint(0,pixels),QPoint(0,angle),
                Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false,Qt::MouseEventNotSynthesized,&seat);
            QCoreApplication::sendEvent(window,&event);QCoreApplication::processEvents();
        };
        wheel(-120,-15);QCOMPARE(scroll->property("contentY").toReal(),88.0);
        QCOMPARE(handler->mapToScene(QPointF()),overlayOrigin);
        wheel(120,15);QCOMPARE(scroll->property("contentY").toReal(),0.0);
        wheel(-120,0);QCOMPARE(scroll->property("contentY").toReal(),88.0);
        for(const auto* name:{"individualSettingsWheelHandler","panelSettingsWheelHandler"})
            QVERIFY(window->findChild<QQuickItem*>(name));
    }
    void windowTrackerSendsBeforeNextGuiEvent() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QByteArray runtime=qgetenv("XDG_RUNTIME_DIR"),signature=qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        auto restore=qScopeGuard([&]{
            runtime.isNull()?qunsetenv("XDG_RUNTIME_DIR"):qputenv("XDG_RUNTIME_DIR",runtime);
            signature.isNull()?qunsetenv("HYPRLAND_INSTANCE_SIGNATURE"):qputenv("HYPRLAND_INSTANCE_SIGNATURE",signature);
        });
        qputenv("XDG_RUNTIME_DIR",dir.path().toUtf8());qputenv("HYPRLAND_INSTANCE_SIGNATURE","fixture");
        const QString base=dir.path()+"/hypr/fixture/";QVERIFY(QDir().mkpath(base));
        QLocalServer server;QVERIFY(server.listen(base+".socket.sock"));
        WindowTracker tracker;tracker.configure(true,false);
        // Run only the queued connection, then keep the GUI event loop paused,
        // as it can be during an EGL/Wayland round trip on startup or resize.
        QCoreApplication::sendPostedEvents(&tracker,QEvent::MetaCall);
        QVERIFY(server.waitForNewConnection(300));
        auto* socket=server.nextPendingConnection();QVERIFY(socket);
        QVERIFY2(socket->bytesAvailable() || socket->waitForReadyRead(300),
                 "Hyprland must receive the command without another GUI event");
        QCOMPARE(socket->readAll(),QByteArray("j/animations"));
        tracker.configure(false,false);
    }
    void windowTrackerIpcAndStaleInput() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QByteArray runtime=qgetenv("XDG_RUNTIME_DIR"),signature=qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        auto restore=qScopeGuard([&]{
            runtime.isNull()?qunsetenv("XDG_RUNTIME_DIR"):qputenv("XDG_RUNTIME_DIR",runtime);
            signature.isNull()?qunsetenv("HYPRLAND_INSTANCE_SIGNATURE"):qputenv("HYPRLAND_INSTANCE_SIGNATURE",signature);
        });
        qputenv("XDG_RUNTIME_DIR",dir.path().toUtf8());qputenv("HYPRLAND_INSTANCE_SIGNATURE","fixture");
        const QString base=dir.path()+"/hypr/fixture/";QVERIFY(QDir().mkpath(base));
        QLocalServer server;QVERIFY(server.listen(base+".socket.sock"));
        bool invalid=false;int snapshots=0;
        connect(&server,&QLocalServer::newConnection,this,[&]{
            while(auto* socket=server.nextPendingConnection()) {
                connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QLocalSocket::readyRead,socket,[&,socket]{
                    const auto command=socket->readAll();QByteArray reply;
                    if(command=="j/animations") reply="[[],[]]";
                    if(command=="[[BATCH]]j/clients;j/monitors;j/cursorpos") {
                        ++snapshots;
                        reply=R"([{"address":"a","mapped":true,"visible":true,"floating":true,"monitor":0,"workspace":{"id":1},"at":[100,200],"size":[400,300]}])";
                        reply+="\n\n\n";
                        reply+=R"([{"id":0,"name":"test","x":0,"y":0,"width":800,"height":600,"scale":1,"activeWorkspace":{"id":1}}])";
                        reply+="\n\n\n";
                        reply+=R"({"x":0,"y":0})";
                    }
                    socket->write(invalid?QByteArray("broken"):reply);socket->disconnectFromServer();
                });
            }
        });
        WindowTracker tracker;QSignalSpy updates(&tracker,&WindowTracker::updated);tracker.configure(true,false);
        QTRY_VERIFY_WITH_TIMEOUT(updates.count()>0,1000);
        unsigned long long generation=0;std::vector<StandingEdge> edges;
        QVERIFY(readStandingEdges(tracker.frame("test").toStdString(),generation,edges));QCOMPARE(edges.size(),size_t(2));
        QVERIFY(snapshots>0);
        const auto previous=generation;invalid=true;updates.clear();QTRY_VERIFY_WITH_TIMEOUT(updates.count()>0,1000);
        QVERIFY(readStandingEdges(tracker.frame("test").toStdString(),generation,edges));QVERIFY(edges.empty());QVERIFY(generation>previous);
        QVERIFY(!readStandingEdges("ARKPETS_EDGES 0 1 a 0 20 nan 0",generation,edges));
        QVERIFY(!readStandingEdges("ARKPETS_EDGES 0 5000",generation,edges));
        tracker.configure(false,false);
    }
    void gravitySettingsAndMaterialControls() {
        QTemporaryDir dir;
        { Controller c(dir.path()+"/data",dir.path()+"/config");
          for(const auto* key:{"gravity","windowEdges","trackMovingWindows"}) QVERIFY(!c.preferences()[key].toBool());
          QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");
          engine.rootContext()->setContextProperty("backend",&c);engine.rootContext()->setContextProperty("trayAvailable",false);
          engine.setInitialProperties({{"currentPage","model-settings"}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
          QVERIFY(!engine.rootObjects().isEmpty());auto* root=engine.rootObjects().first();
          auto* edges=root->findChild<QObject*>("windowEdgesSwitch");auto* gravity=root->findChild<QObject*>("gravitySwitch");
          auto* moving=root->findChild<QObject*>("trackMovingWindowsSwitch");QVERIFY(edges&&gravity&&moving);
          edges->setProperty("checked",true);QVERIFY(QMetaObject::invokeMethod(edges,"clicked"));
          QVERIFY(root->property("draftGravity").toBool());QVERIFY(root->property("draftWindowEdges").toBool());
          root->setProperty("draftTrackMovingWindows",true);QVERIFY(QMetaObject::invokeMethod(root,"saveSettings"));
          QVERIFY(c.preferences()["gravity"].toBool());QVERIFY(c.preferences()["windowEdges"].toBool());
          gravity->setProperty("checked",false);QVERIFY(QMetaObject::invokeMethod(gravity,"clicked"));
          QVERIFY(!root->property("draftWindowEdges").toBool());
        }
        Controller restored(dir.path()+"/data",dir.path()+"/config");
        QVERIFY(restored.preferences()["gravity"].toBool());QVERIFY(restored.preferences()["windowEdges"].toBool());
        QVERIFY(restored.preferences()["trackMovingWindows"].toBool());
        restored.savePreferences({{"gravity",false}});QVERIFY(!restored.preferences()["windowEdges"].toBool());
        restored.savePreferences({{"windowEdges",true}});QVERIFY(restored.preferences()["gravity"].toBool());
    }
    void liveWindowTracker() {
        if (!qEnvironmentVariableIsSet("ARKPETS_LIVE_HYPRLAND")) QSKIP("Enable only for a read-only Hyprland IPC check");
        WindowTracker tracker;QSignalSpy updates(&tracker,&WindowTracker::updated);tracker.configure(true,false);
        QTRY_VERIFY_WITH_TIMEOUT(updates.count()>=3,3000);
        bool found=false;size_t count=0;
        for(const auto* screen:QGuiApplication::screens()) {
            unsigned long long generation=0;std::vector<StandingEdge> edges;
            QVERIFY(readStandingEdges(tracker.frame(screen->name()).toStdString(),generation,edges));
            count+=edges.size();found|=!edges.empty();
        }
        QVERIFY2(found,"No usable window edges found on the active outputs");
        qInfo()<<"Read"<<count<<"window edges through the live asynchronous IPC client";
        tracker.configure(false,false);
    }
    void uniformScaleIndependentOfModelAndActionBounds() {
        ModelBounds relax;relax.include(-40,0);relax.include(40,100);
        ModelBounds all=relax;all.include(-90,-50);all.include(150,130);all.includeMirror();
        ModelViewport manual(relax,1.25),automatic(all,1.25);
        QCOMPARE(manual.scale,1.25);QCOMPARE(automatic.scale,manual.scale);
        QVERIFY(automatic.width>manual.width);QVERIFY(automatic.height>manual.height);
        auto renderedHeight=[](const ModelViewport& view,float bottom,float top) {
            return (view.projectY(top,view.height)-view.projectY(bottom,view.height))*view.height/2;
        };
        QVERIFY(std::abs(renderedHeight(manual,0,100)-125)<.001);
        QVERIFY(std::abs(renderedHeight(automatic,0,100)-125)<.001);
        // A taller model keeps its proportions; horns and other attachments
        // may enlarge the canvas but must not shrink the same 100-unit body.
        ModelBounds taller=all;taller.include(0,200);
        ModelViewport otherModel(taller,1.25);
        QCOMPARE(otherModel.scale,manual.scale);
        QVERIFY(std::abs(renderedHeight(otherModel,0,100)-125)<.001);
        QVERIFY(std::abs(renderedHeight(otherModel,0,200)-250)<.001);
        QVERIFY(automatic.projectY(-50,automatic.height)>-1);
        QVERIFY(automatic.projectY(130,automatic.height)<1);
        QVERIFY(automatic.projectX(-150,automatic.width)>-1);
        QVERIFY(automatic.projectX(150,automatic.width)<1);
        ModelViewport half(all,.625);
        QCOMPARE(half.scale,automatic.scale/2);
        QVERIFY(std::abs(renderedHeight(half,0,100)-62.5)<.001);
        QVERIFY_EXCEPTION_THROWN(ModelViewport(all,0),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ModelViewport(all,std::numeric_limits<double>::quiet_NaN()),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ModelViewport(all,1e10),std::runtime_error);
    }
    void rendererScaleArguments() {
        const QString renderer=QCoreApplication::applicationDirPath()+"/arkpets-renderer";
        for (const QString& value:{QString("0.0625"),QString("0.75"),QString("1"),QString("6.25")}) {
            QProcess p;p.start(renderer,{"--scale",value,"--help"});QVERIFY(p.waitForFinished(3000));
            QCOMPARE(p.exitCode(),0);auto help=p.readAllStandardOutput();
            QVERIFY(help.contains("--scale"));QVERIFY(!help.contains("--size"));
        }
        for (const QString& value:{QString("0"),QString("-1"),QString("0.0624"),QString("6.26"),QString("nan"),QString("inf"),QString("0.75x")}) {
            QProcess p;p.start(renderer,{"--scale",value,"--help"});QVERIFY(p.waitForFinished(3000));
            QVERIFY(p.exitCode()!=0);
        }
        QProcess p;p.start(renderer,{"--right","-350","--bottom","-80","--help"});
        QVERIFY(p.waitForFinished(3000));QCOMPARE(p.exitCode(),0);
    }
    void defaultMovementBoundsAllowEffectsOffscreen() {
        ModelBounds body;body.include(-20,0);body.include(80,100);
        ModelBounds effects=body;effects.include(-200,-50);effects.include(200,900);
        ModelViewport view(effects,1.0);
        const auto limits=view.movementLimits(body,800,600,false);
        // The 982-pixel canvas exceeds the monitor, but the 100-pixel Default
        // body can still travel from the bottom edge all the way to the top.
        QVERIFY(view.height>600);
        QCOMPARE(limits.maxBottom-limits.minBottom,500);
        QCOMPARE(limits.maxRight-limits.minRight,700);
        QVERIFY(limits.minBottom<0);QVERIFY(limits.minRight<0);
        auto bodyTop=[&](int margin){return 600-margin-(body.maxY-view.minY)*view.scale;};
        auto bodyBottom=[&](int margin){return 600-margin-(body.minY-view.minY)*view.scale;};
        QCOMPARE(bodyTop(limits.maxBottom),0.0);QCOMPARE(bodyBottom(limits.minBottom),600.0);
        const auto flipped=view.movementLimits(body,800,600,true);
        QCOMPARE(flipped.maxRight-flipped.minRight,700);
        QCOMPARE(flipped.minRight,limits.minRight-60);
        const auto oversized=view.movementLimits(body,50,50,false);
        QCOMPARE(oversized.minRight,oversized.maxRight);QCOMPARE(oversized.minBottom,oversized.maxBottom);
        // Automatic walking must support the same signed margin interval.
        ActivityDirector movement(activityPlan({"Default","Move"},"Default"),-150,7);
        bool walked=false;
        for(int n=0;n<200;++n) {
            movement.advance(.1,-100,60,-200);walked|=movement.walking();
            QVERIFY(movement.right()>=-200&&movement.right()<=-100);
        }
        QVERIFY(walked);
        movement.advance(0,-180,0,-200);QVERIFY(movement.right()<=-180);
    }
    void activityTimingBoundsAndFallback() {
        auto plan=activityPlan({"Relax","Move","Sit","Sleep","Interact"},"Interact");
        ActivityDirector slow(plan,64,42),fast(plan,64,42);
        std::set<ActivityKind> seen;
        for(int second=0;second<900;++second) {
            slow.advance(1,100,57);
            for(int frame=0;frame<120;++frame) fast.advance(1.0/120,100,57);
            QVERIFY(slow.right()>=0&&slow.right()<=100);
            QVERIFY(std::abs(slow.right()-fast.right())<1e-6);
            QCOMPARE(slow.animation(),fast.animation());
            seen.insert(slow.kind());
        }
        QCOMPARE(seen.size(),std::size_t(4));
        slow.pause();double paused=slow.right();slow.advance(100,100,57);
        QCOMPARE(slow.right(),paused);QVERIFY(!slow.walking());
        slow.resumeAt(30);slow.advance(1,100,57);QCOMPARE(slow.right(),30.0);
        ActivityDirector fallback(activityPlan({"Attack"},"Attack"),20,1);
        for(int n=0;n<100;++n) {fallback.advance(10,100,60);QCOMPARE(fallback.animation(),std::string("Attack"));QCOMPARE(fallback.right(),20.0);}
        ActivityDirector noRoom(plan,64,2);
        for(int n=0;n<100;++n) {noRoom.advance(10,0,60);QCOMPARE(noRoom.right(),0.0);QVERIFY(!noRoom.walking());}
        QCOMPARE(activityPlan({"idle","walk","sit","sleep"},"idle").size(),std::size_t(4));
        QCOMPARE(activityPlan({"Default","RELAX","Idle"},"Default").front().animation,std::string("RELAX"));
        QCOMPARE(findAnimation({"relax","INTERACT"},{"interact"}),std::string("INTERACT"));
        QVERIFY(findAnimation({"Relax"},{"interact"}).empty());
    }
    void interactionChanceAndMissingAnimations() {
        const auto interact=findAnimation({"Relax","INTERACT","sPeCiAl"},{"interact"});
        const auto special=findAnimation({"Relax","INTERACT","sPeCiAl"},{"special"});
        QCOMPARE(interact,std::string("INTERACT"));QCOMPARE(special,std::string("sPeCiAl"));
        std::mt19937 random(42);int specials=0,adjacentSpecials=0;bool previous=false;
        for(int n=0;n<10000;++n) {
            const auto& picked=chooseInteraction(interact,special,random);
            QVERIFY(picked==interact || picked==special);
            bool isSpecial=picked==special;specials+=isSpecial;adjacentSpecials+=isSpecial&&previous;previous=isSpecial;
        }
        QVERIFY2(specials>1800 && specials<2200,qPrintable(QString("Special count: %1 / 10000").arg(specials)));
        QVERIFY(adjacentSpecials>0); // Independent probability, not a five-click counter.
        for(int n=0;n<100;++n) QCOMPARE(chooseInteraction(interact,{},random),interact);
        QVERIFY(chooseInteraction({},{},random).empty());
        std::mt19937 specialSeed(7),regularSeed(0);
        QCOMPARE(chooseInteraction({},special,specialSeed),special);
        QVERIFY(chooseInteraction({},special,regularSeed).empty());
    }
    void materialSettingsInteraction() {
        QTemporaryDir dir;
        // Exercise a real INI reload, including the text "false" written by Qt.
        QDir().mkpath(dir.path()+"/config");QFile ini(dir.path()+"/config/settings.ini");
        QVERIFY(ini.open(QIODevice::WriteOnly));ini.write("[General]\nautoActivity=false\nclickThrough=false\nflip=false\n");ini.close();
        Controller c(dir.path()+"/data",dir.path()+"/config");
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");
        engine.rootContext()->setContextProperty("backend",&c);engine.rootContext()->setContextProperty("trayAvailable",false);
        engine.setInitialProperties({{"currentPage","model-settings"}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        auto* scale=window->findChild<QObject*>("scaleField");auto* slider=window->findChild<QObject*>("scaleSlider");
        auto* automatic=window->findChild<QObject*>("autoActivitySwitch");
        QVERIFY(scale&&slider&&automatic);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(window->flags().testFlag(Qt::FramelessWindowHint));
        auto click=[window](QObject* object) {
            auto* item=qobject_cast<QQuickItem*>(object);
            if(!item || !item->isVisible()) return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
            return true;
        };
        QVERIFY(!automatic->property("checked").toBool());
        scale->setProperty("text","0.75");QVERIFY(QMetaObject::invokeMethod(scale,"commit"));
        QCOMPARE(window->property("draftScale").toDouble(),.75);QCOMPARE(slider->property("value").toDouble(),.75);
        QVERIFY(QMetaObject::invokeMethod(slider,"setValue",Q_ARG(QVariant,1.5)));
        QCOMPARE(window->property("draftScale").toDouble(),1.5);QCOMPARE(scale->property("value").toDouble(),1.5);
        QVERIFY(click(automatic));QVERIFY(window->property("draftAutoActivity").toBool());
        QVERIFY(QMetaObject::invokeMethod(window,"loadPreferences"));
        QCOMPARE(scale->property("value").toDouble(),1.0);QCOMPARE(slider->property("value").toDouble(),1.0);
        QVERIFY(!automatic->property("checked").toBool());
        QVERIFY(click(automatic));
        QVERIFY(QMetaObject::invokeMethod(window,"saveSettings"));QVERIFY(c.preferences()["autoActivity"].toBool());
        // Real text editing and Save while the TextInput still owns focus.
        auto* input=qvariant_cast<QQuickItem*>(scale->property("input"));QVERIFY(input);
        input->forceActiveFocus();QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);for (auto key:{Qt::Key_1,Qt::Key_Period,Qt::Key_2,Qt::Key_5}) QTest::keyClick(window,key);
        QCOMPARE(window->property("draftScale").toDouble(),1.25);QCOMPARE(slider->property("value").toDouble(),1.25);
        QVERIFY(click(window->findChild<QObject*>("saveSettings")));QCOMPARE(c.preferences()["scale"].toDouble(),1.25);
        // A slider change must commit after leaving an edited text field.
        QVERIFY(click(slider));double sliderScale=window->property("draftScale").toDouble();
        QVERIFY(click(window->findChild<QObject*>("saveSettings")));QCOMPARE(c.preferences()["scale"].toDouble(),sliderScale);
        auto* layer=window->findChild<QObject*>("layerPicker");QVERIFY(layer);
        QVERIFY(click(layer));QTest::qWait(250);
        QQuickItem* option=nullptr;
        std::function<void(QQuickItem*)> findOption=[&](QQuickItem* item) {
            if(item->property("text").toString()=="覆盖全屏窗口" && item->isVisible()) option=item;
            for(auto* child:item->childItems()) findOption(child);
        };
        findOption(window->contentItem());
        QVERIFY(option);QVERIFY(click(option));
        QCOMPARE(window->property("draftLayer").toString(),QString("overlay"));
        QTest::qWait(250);
        QVERIFY(QMetaObject::invokeMethod(window,"loadPreferences"));QCOMPARE(layer->property("currentIndex").toInt(),0);
        QVERIFY(click(visualItem(window->contentItem(),"nav-downloaded")));QCOMPARE(window->property("currentPage").toString(),QString("downloaded"));
        QVERIFY(click(visualItem(window->contentItem(),"nav-model-settings")));QCOMPARE(window->property("currentPage").toString(),QString("model-settings"));
        QVERIFY(click(window->findChild<QObject*>("hideToTrayButton")));QVERIFY(!window->isVisible());
        window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        // Compositor close requests have the same Hide behavior, including without a tray host.
        QVERIFY(!window->close());QVERIFY(!window->isVisible());
    }

    void materialScrollbarDrag() {
        QTemporaryDir dir;
        QDir().mkpath(dir.path()+"/data");
        auto catalog=QJsonDocument::fromJson(fixture()).object();
        auto entry=catalog["data"].toObject()["2025_shu"].toObject();
        QJsonObject models;
        for(int n=0;n<600;++n) {
            entry["name"]=QString("角色 %1").arg(n,3,10,QChar('0'));
            models[QString("scroll_%1").arg(n)]=entry;
        }
        catalog["data"]=models;
        QDir().mkpath(dir.path()+"/config");
        QSettings oldSettings(dir.path()+"/config/panel.ini",QSettings::IniFormat);
        oldSettings.setValue("wheelRows",12);oldSettings.sync();
        QFile cache(dir.path()+"/data/catalog.json");
        QVERIFY(cache.open(QIODevice::WriteOnly));cache.write(QJsonDocument(catalog).toJson());cache.close();
        Controller c(dir.path()+"/data",dir.path()+"/config");
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");
        engine.rootContext()->setContextProperty("backend",&c);
        engine.load(QUrl("qrc:/ui/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* list=window->findChild<QQuickItem*>("modelList");
        auto* bar=window->findChild<QQuickItem*>("modelScrollBar");QVERIFY(list&&bar);
        QTRY_VERIFY(bar->isVisible());QTest::qWait(80);
        auto position=[list] {return list->property("contentY").toReal();};
        const QPoint start=bar->mapToScene(QPointF(bar->width()/2,34)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        auto release=qScopeGuard([&] {QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start);});
        qreal previous=position();
        for(int step=1;step<=20;++step) {
            QTest::mouseMove(window,start+QPoint(-std::max(0,step-4)*6,step*8),16);
            QCoreApplication::processEvents();
            QVERIFY2(bar->property("isPressed").toBool(),"The list stole the scrollbar's mouse grab");
            QVERIFY2(!list->property("dragging").toBool(),"Scrollbar movement became a content drag");
            QVERIFY2(position()>=previous-1,"Dragging the scrollbar down reversed the list");
            previous=position();
        }
        QVERIFY(position()>1000);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(0,160));
        release.dismiss();
        QTest::qWait(240);QVERIFY(std::abs(position()-previous)<1);

        // Clicking empty track and continuing the same gesture must start there,
        // instead of reusing the previous thumb drag's offset and jumping back.
        const QPoint track=bar->mapToScene(QPointF(bar->width()/2,bar->height()*.7)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,track);
        auto releaseTrack=qScopeGuard([&] {QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,track);});
        QCoreApplication::processEvents();previous=position();
        for(int step=1;step<=10;++step) {
            QTest::mouseMove(window,track+QPoint(-step*4,step*8),16);
            QCoreApplication::processEvents();
            QVERIFY(bar->property("isPressed").toBool());
            QVERIFY2(position()>=previous-1,"Track click followed by drag jumped backwards");
            previous=position();
        }
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,track+QPoint(-40,80));
        releaseTrack.dismiss();
        QTest::qWait(240);QVERIFY(std::abs(position()-previous)<1);

        // Moving the thumb up must still work after the pointer has left it.
        const QPoint lowerTrack=bar->mapToScene(QPointF(bar->width()/2,bar->height()*.8)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,lowerTrack);
        auto releaseUp=qScopeGuard([&] {QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,lowerTrack);});
        QCoreApplication::processEvents();previous=position();
        for(int step=1;step<=12;++step) {
            QTest::mouseMove(window,lowerTrack+QPoint(-step*5,-step*8),16);
            QCoreApplication::processEvents();
            QVERIFY(bar->property("isPressed").toBool());
            QVERIFY(position()<=previous+1);previous=position();
        }
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,lowerTrack+QPoint(-60,-96));
        releaseUp.dismiss();
        QTest::qWait(240);QVERIFY(std::abs(position()-previous)<1);

        // Native wheel events over a card: one row per notch, even with an old saved speed,
        // with pixel-based touchpad events retaining their fine movement.
        const QPointF body=list->mapToScene(QPointF(list->width()/2,list->height()/2));
        QPointingDevice mouse("Test mouse",100,QInputDevice::DeviceType::Mouse,
            QPointingDevice::PointerType::Generic,QInputDevice::Capability::Position,1,3);
        QPointingDevice touchpad("Test touchpad",101,QInputDevice::DeviceType::TouchPad,
            QPointingDevice::PointerType::Finger,QInputDevice::Capability::Position,5,0);
        ulong wheelTime=1000;
        auto wheel=[&](int angle,int pixels,Qt::ScrollPhase phase,const QPointingDevice* device,
                       Qt::MouseEventSource source=Qt::MouseEventNotSynthesized) {
            QWheelEvent event(body,window->mapToGlobal(body.toPoint()),QPoint(0,pixels),QPoint(0,angle),
                Qt::NoButton,Qt::NoModifier,phase,false,source,device);
            event.setTimestamp(wheelTime+=16);
            QCoreApplication::sendEvent(window,&event);QCoreApplication::processEvents();
        };
        list->setProperty("contentY",1000);previous=position();
        wheel(-120,0,Qt::NoScrollPhase,&mouse);
        QCOMPARE(position()-previous,88.0);
        wheel(120,0,Qt::NoScrollPhase,&mouse);QVERIFY(std::abs(position()-previous)<1);
        wheel(-60,0,Qt::NoScrollPhase,&mouse);
        QCOMPARE(position()-previous,44.0);
        previous=position();
        wheel(-120,-15,Qt::NoScrollPhase,&mouse);
        QCOMPARE(position()-previous,88.0);
        previous=position();
        wheel(0,-15,Qt::NoScrollPhase,&mouse);
        QCOMPARE(position()-previous,88.0);
        // The shared Hyprland seat can label a physical wheel as TouchPad.
        previous=position();
        wheel(-120,-15,Qt::NoScrollPhase,&touchpad);
        QCOMPARE(position()-previous,88.0);
        // Settle delegate layout before starting a separate touchpad gesture.
        QTest::qWait(50);
        previous=position();
        wheel(0,0,Qt::ScrollBegin,&touchpad);
        // Include enough frames to cross Flickable's drag-start threshold.
        for (int n=0;n<4;++n)
            wheel(-120,-24,Qt::ScrollUpdate,&touchpad,Qt::MouseEventSynthesizedBySystem);
        QTest::qWait(20);
        QVERIFY2(position()>previous&&position()-previous<=96,
            qPrintable(QString("Touchpad pixel delta unexpectedly moved %1 px").arg(position()-previous)));
        wheel(0,0,Qt::ScrollEnd,&touchpad,Qt::MouseEventSynthesizedBySystem);
        QMetaObject::invokeMethod(list,"cancelFlick");
        previous=position();
        // Continuous Wayland axes can omit phases; their source still identifies
        // them as system-generated scrolling and must retain pixel precision.
        wheel(-120,-15,Qt::NoScrollPhase,&touchpad,Qt::MouseEventSynthesizedBySystem);
        QTest::qWait(20);
        QVERIFY(position()-previous<60);
        QMetaObject::invokeMethod(list,"cancelFlick");
        const qreal beginning=list->property("originY").toReal();
        const qreal end=beginning+list->property("contentHeight").toReal()-list->height();
        list->setProperty("contentY",end-10);wheel(-120,0,Qt::NoScrollPhase,&mouse);
        QVERIFY(std::abs(position()-end)<1);
        list->setProperty("contentY",beginning+10);wheel(120,0,Qt::NoScrollPhase,&mouse);
        QVERIFY(std::abs(position()-beginning)<1);

        // Regular dragging inside a card still scrolls the list after release.
        list->setProperty("contentY",1000);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,body.toPoint());
        for(int step=1;step<=8;++step) QTest::mouseMove(window,body.toPoint()+QPoint(0,-step*10),16);
        QVERIFY(list->property("dragging").toBool());QVERIFY(position()>1010);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,body.toPoint()+QPoint(0,-80));

        const QString output=qEnvironmentVariable("ARKPETS_TEST_OUTPUT_DIR");
        if(!output.isEmpty()) {
            QDir().mkpath(output);QFile report(output+"/scrollbar-report.json");QVERIFY(report.open(QIODevice::WriteOnly));
            report.write(QJsonDocument(QJsonObject{{"result","PASS"},{"input_method","QtTest mouse / QWheelEvent"},
                {"platform",QGuiApplication::platformName()},{"model_count",600},{"drag_into_list",true},
                {"track_click_then_drag",true},{"upward_drag",true},{"content_drag_preserved",true},
                {"mouse_wheel_rows_per_notch",1},{"legacy_wheel_speed_ignored",true},{"touchpad_pixel_scroll_preserved",true},{"endpoints_clamped",true}}).toJson());
        }


    }

    void schemaSearchAndEncoding() {
        QTemporaryDir dir;CatalogModel catalog(dir.path());QString error;
        QVERIFY2(catalog.load(fixture(),&error),qPrintable(error));QCOMPARE(catalog.rowCount(),4);
        CatalogFilter filter;filter.setSourceModel(&catalog);
        filter.setQuery("黍","Operator",false);QCOMPARE(filter.rowCount(),2);
        filter.setQuery("sHu","Operator",false);QCOMPARE(filter.rowCount(),2);
        filter.setQuery("0011 XI","Operator",false);QCOMPARE(filter.rowCount(),1);
        filter.setQuery("","DynIllust",false);QCOMPARE(filter.rowCount(),1);
        filter.setQuery("","",true);QCOMPARE(filter.rowCount(),0);
        const auto* skin=catalog.find("2025_shu_nian#11");QVERIFY(skin&&skin->available);
        QUrl url=CatalogModel::assetUrl(QUrl("https://example.com/main/"),*skin,skin->files[".skel"].toString());
        QVERIFY(url.fragment().isEmpty());QVERIFY(url.toEncoded().contains("%23"));
        QCOMPARE(url.path(),QString("/main/models/2025_shu_nian#11/build_char_2025_shu_nian#11.skel"));
        QVERIFY(!catalog.load("{broken",&error));QCOMPARE(catalog.rowCount(),4);
        QVERIFY(!CatalogModel::safeName("../escape"));QVERIFY(!CatalogModel::safeName(".."));
        auto object=QJsonDocument::fromJson(fixture()).object();auto data=object["data"].toObject();
        auto model=data["2025_shu"].toObject();auto files=model["assetList"].toObject();
        files[".png"]="../outside.png";model["assetList"]=files;data["2025_shu"]=model;object["data"]=data;
        QVERIFY(catalog.load(QJsonDocument(object).toJson(),&error));QVERIFY(!catalog.find("2025_shu")->available);
    }
    void settingsPersistAndValidate() {
        QTemporaryDir dir;
        {Controller c(dir.path()+"/data",dir.path()+"/config");
            c.savePreferences({{"scale",1.04},{"fps",60},{"autoActivity",true},{"clickThrough",true},{"flip",true},{"monitor","DP-4"},{"layer","bottom"}});
            QVERIFY(c.error().isEmpty());
            for (double invalid:{0.0,.249,2.501,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
                c.savePreferences({{"scale",invalid},{"fps",60}});QVERIFY(!c.error().isEmpty());
                QCOMPARE(c.preferences()["scale"].toDouble(),1.04);
            }
        }
        Controller restored(dir.path()+"/data",dir.path()+"/config");
        QCOMPARE(restored.preferences()["scale"].toDouble(),1.04);QCOMPARE(restored.preferences()["fps"].toInt(),60);
        QVERIFY(restored.preferences()["clickThrough"].toBool());QCOMPARE(restored.preferences()["layer"].toString(),QString("bottom"));
        QVERIFY(restored.preferences()["autoActivity"].toBool());
        QCOMPARE(restored.downloadedCount(),0);
    }
    void legacyHeightRetiredWithoutChangingRelativeSettings() {
        for (bool hasScale:{false,true}) {
            QTemporaryDir dir;const QString config=dir.path()+"/config";
            QDir().mkpath(config);
            {
                QSettings ini(config+"/settings.ini",QSettings::IniFormat);
                ini.setValue("size",288);ini.setValue("fps",48);ini.setValue("autoActivity",true);
                ini.setValue("models/2025_shu/multiplier",1.25);
                ini.setValue("models/2025_shu/monitor","DP-3");
                ini.setValue("models/2025_shu/enabled",true);
                if(hasScale) ini.setValue("scale",.83);
                ini.sync();
            }
            {
                Controller c(dir.path()+"/data",config);
                QCOMPARE(c.preferences()["scale"].toDouble(),hasScale?.83:1.0);
                QVERIFY(!c.preferences().contains("size"));
                QCOMPARE(c.preferences()["fps"].toInt(),48);QVERIFY(c.preferences()["autoActivity"].toBool());
                QSettings ini(config+"/settings.ini",QSettings::IniFormat);
                QVERIFY(!ini.contains("size"));
                QCOMPARE(ini.value("models/2025_shu/multiplier").toDouble(),1.25);
                QCOMPARE(ini.value("models/2025_shu/monitor").toString(),QString("DP-3"));
                QVERIFY(ini.value("models/2025_shu/enabled").toBool());
                c.savePreferences({{"scale",.65}});
            }
            Controller restored(dir.path()+"/data",config);
            QCOMPARE(restored.preferences()["scale"].toDouble(),.65);
        }
    }
    void individualSettingsInheritanceAndPersistence() {
        QTemporaryDir dir;
        const QString root=dir.path()+"/data",config=dir.path()+"/config";
        QDir().mkpath(root);QDir().mkpath(config);
        auto write=[](const QString& path,const QByteArray& bytes) {
            QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();
        };
        QVERIFY(write(root+"/catalog.json",fixture()));
        auto entries=QJsonDocument::fromJson(fixture()).object()["data"].toObject();
        for (const QString& id:{QString("2025_shu"),QString("2025_shu_nian#11")}) {
            const QString folder=root+"/models/"+id;QDir().mkpath(folder);
            QVERIFY(write(folder+"/complete.json",QJsonDocument(QJsonObject{{"id",id}}).toJson()));
            for (const auto& value:entries[id].toObject()["assetList"].toObject()) QVERIFY(write(folder+'/'+value.toString(),"fixture"));
        }
        QVERIFY(write(config+"/settings.ini","[General]\nlastModel=2025_shu\nsize=400\nfps=60\nright=142\nbottom=81\nautoActivity=true\n"));
        const QString a="2025_shu",b="2025_shu_nian#11";
        {
            Controller c(root,config);c.clearError(); // Metadata-only fixtures are not renderable.
            QCOMPARE(c.pets().size(),2);QVERIFY(!c.modelSettings(a)["enabled"].toBool());
            QCOMPARE(c.modelSettings(a)["effective"].toMap()["right"].toInt(),142);
            QVERIFY(c.modelSettings(b)["effective"].toMap()["right"].toInt()!=142);
            c.saveModelSettings(a,{{"multiplier",.75},{"overrides",QVariantMap{{"monitor","DP-3"},{"layer","overlay"},{"autoActivity",false},{"flip",true}}}});
            QVERIFY2(c.error().isEmpty(),qPrintable(c.error()));
            c.savePreferences({{"scale",1.5},{"fps",30},{"monitor","DP-4"},{"autoActivity",true}});
            auto own=c.modelSettings(a),inherited=c.modelSettings(b)["effective"].toMap();
            auto effective=own["effective"].toMap();
            QCOMPARE(effective["scale"].toDouble(),1.125);QCOMPARE(effective["fps"].toInt(),30);
            QCOMPARE(effective["monitor"].toString(),QString("DP-3"));QVERIFY(!effective["autoActivity"].toBool());
            QCOMPARE(inherited["scale"].toDouble(),1.5);QCOMPARE(inherited["monitor"].toString(),QString("DP-4"));
            QVERIFY(inherited["autoActivity"].toBool());QCOMPARE(inherited["layer"].toString(),QString("top"));
            QVERIFY(!own["overrides"].toMap().contains("fps"));
            c.saveModelSettings(b,{{"multiplier",0.0},{"overrides",QVariantMap{}}});QVERIFY(!c.error().isEmpty());
            QCOMPARE(c.modelSettings(b)["multiplier"].toDouble(),1.0);
        }
        Controller restored(root,config);restored.clearError();
        auto own=restored.modelSettings(a);QCOMPARE(own["multiplier"].toDouble(),.75);
        QCOMPARE(own["overrides"].toMap()["autoActivity"].metaType().id(),QMetaType::Bool);
        QVERIFY(!own["overrides"].toMap()["autoActivity"].toBool());
        QCOMPARE(own["effective"].toMap()["right"].toInt(),142);
        restored.saveModelSettings(a,{{"multiplier",1.0},{"overrides",QVariantMap{}},{"fps",120}});
        QVERIFY(restored.modelSettings(a)["overrides"].toMap().isEmpty());
        QCOMPARE(restored.modelSettings(a)["effective"].toMap()["fps"].toInt(),30);
        restored.savePreferences({{"scale",.25}});
        restored.saveModelSettings(a,{{"multiplier",.25},{"overrides",QVariantMap{}}});
        QCOMPARE(restored.modelSettings(a)["effective"].toMap()["scale"].toDouble(),.0625);
        restored.savePreferences({{"scale",2.5}});
        restored.saveModelSettings(a,{{"multiplier",2.5},{"overrides",QVariantMap{}}});
        QCOMPARE(restored.modelSettings(a)["effective"].toMap()["scale"].toDouble(),6.25);
        restored.selectModel(b);restored.removeSelected();QCOMPARE(restored.pets().size(),1);
        QCOMPARE(restored.modelSettings(a)["multiplier"].toDouble(),2.5);
    }
    void panelPalettePersistenceAndReload() {
        QTemporaryDir dir;
        const QString paletteFile=dir.path()+"/palette/colors.json";
        QDir().mkpath(QFileInfo(paletteFile).absolutePath());
        StyleManager style;
        auto exportScheme=[](const QVariantMap& map) {
            QJsonObject json;
            for (auto it=map.cbegin();it!=map.cend();++it) {
                QString name=it.key();
                if (name.startsWith("on") && name.endsWith("Color")) name.chop(5);
                QString snake;
                for (QChar c : name) {if(c.isUpper()) snake+='_'; snake+=c.toLower();}
                json[snake]=QColor(it.value().toString()).name(QColor::HexRgb);
            }
            return json;
        };
        QJsonObject colors{{"light",exportScheme(style.preview(QColor("#f2abbe"),false))},
                           {"dark",exportScheme(style.preview(QColor("#f2abbe"),true))}};
        auto write=[&](const QByteArray& bytes) {
            QSaveFile file(paletteFile);
            return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
        };
        QVERIFY(write(QJsonDocument(QJsonObject{{"mode","dark"},{"colors",colors}}).toJson()));
        {
            PanelSettings panel(dir.path()+"/config",{paletteFile},QStringLiteral("/usr/bin/true"));
            QCOMPARE(panel.mode(),QString("system"));
            panel.setPalette("matugen");QVERIFY(!panel.lightScheme().isEmpty());
            QCOMPARE(panel.darkScheme()["primary"].toString(),colors["dark"].toObject()["primary"].toString());
            QCOMPARE(panel.lightScheme()["onPrimaryColor"].toString(),colors["light"].toObject()["on_primary"].toString());
            panel.setMode("dark");
            panel.setMode("invalid");panel.setPalette("invalid");
            QCOMPARE(panel.mode(),QString("dark"));QCOMPARE(panel.palette(),QString("matugen"));
            auto light=colors["light"].toObject();light["primary"]="#112233";colors["light"]=light;
            QVERIFY(write(QJsonDocument(QJsonObject{{"mode","light"},{"colors",colors}}).toJson()));
            QTRY_COMPARE(panel.lightScheme()["primary"].toString(),QString("#112233"));
            QVERIFY(panel.dark()); // A system palette refresh never overrides explicit dark mode.
            QVERIFY(write("{malformed"));QTRY_VERIFY(panel.lightScheme().isEmpty());
            QCOMPARE(panel.seed(),QString("#496b45"));QVERIFY(panel.systemStatus().contains("默认"));
            // The upstream per-role export is supported too.
            QJsonObject transposed;
            const auto dark=colors["dark"].toObject();
            for(auto it=light.begin();it!=light.end();++it) transposed[it.key()]=QJsonObject{{"light",it.value()},{"dark",dark[it.key()]}};
            QVERIFY(write(QJsonDocument(QJsonObject{{"colors",transposed}}).toJson()));
            QTRY_COMPARE(panel.lightScheme()["primary"].toString(),QString("#112233"));
        }
        PanelSettings restored(dir.path()+"/config",{paletteFile},QStringLiteral("/usr/bin/true"));
        QCOMPARE(restored.mode(),QString("dark"));QCOMPARE(restored.palette(),QString("matugen"));
        PanelSettings absent(dir.path()+"/config",{paletteFile},QStringLiteral(""));
        QVERIFY(absent.lightScheme().isEmpty());QVERIFY(absent.systemStatus().contains("未安装"));
        QCOMPARE(absent.seed(),QString("#496b45"));
        style.configure(QColor("#808080"),true,{},{});
        const QColor mono(style.lightScheme()["primary"].toString());
        QCOMPARE(mono.red(),mono.green());QCOMPARE(mono.green(),mono.blue());
    }
    void sidebarSettingsPersistWithoutChangingTheme() {
        QTemporaryDir dir;
        {
            PanelSettings panel(dir.path());
            QVERIFY(!panel.sidebarQuickAdjust());QVERIFY(!panel.sidebarEnabledOnly());
            panel.setMode("dark");panel.setPalette("purple");
            QSignalSpy appearance(&panel,&PanelSettings::changed),sidebar(&panel,&PanelSettings::sidebarChanged);
            panel.setSidebarQuickAdjust(true);panel.setSidebarEnabledOnly(true);
            QCOMPARE(sidebar.count(),2);QCOMPARE(appearance.count(),0);
            QCOMPARE(panel.mode(),QString("dark"));QCOMPARE(panel.palette(),QString("purple"));
        }
        {
            PanelSettings restored(dir.path());
            QVERIFY(restored.sidebarQuickAdjust());QVERIFY(restored.sidebarEnabledOnly());
            restored.setSidebarQuickAdjust(false);restored.setSidebarEnabledOnly(false);
        }
        PanelSettings restored(dir.path());
        QVERIFY(!restored.sidebarQuickAdjust());QVERIFY(!restored.sidebarEnabledOnly());
        QCOMPARE(restored.mode(),QString("dark"));QCOMPARE(restored.palette(),QString("purple"));
    }
    void sidebarScrollingFilteringAndControls() {
        QTemporaryDir dir;
        const QString root=dir.path()+"/data",config=dir.path()+"/config";
        QDir().mkpath(root);QDir().mkpath(config);
        auto write=[](const QString& path,const QByteArray& data){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        auto catalog=QJsonDocument::fromJson(fixture()).object();
        auto sample=catalog["data"].toObject()["2025_shu"].toObject();QJsonObject records;
        QStringList ids;
        for(int i=0;i<24;++i) {
            QString id=QString("sidebar_%1").arg(i,2,10,QChar('0'));ids<<id;
            auto entry=sample;entry["name"]=QString("模型 %1").arg(i,2,10,QChar('0'));records[id]=entry;
            QString folder=root+"/models/"+id;QDir().mkpath(folder);
            QVERIFY(write(folder+"/complete.json",QJsonDocument(QJsonObject{{"id",id}}).toJson()));
            for(auto file:entry["assetList"].toObject())QVERIFY(write(folder+'/'+file.toString(),"metadata fixture"));
        }
        catalog["data"]=records;QVERIFY(write(root+"/catalog.json",QJsonDocument(catalog).toJson()));
        {QSettings settings(config+"/settings.ini",QSettings::IniFormat);
            settings.setValue("lastModel","missing");settings.setValue("petsSchema",1);
            // Only a few rows are enabled; no renderer is restored by this test.
            for(int i:{3,12,22})settings.setValue("models/"+ids[i]+"/enabled",true);
            settings.sync();}
        Controller c(root,config);QCOMPARE(c.pets().size(),24);
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");engine.rootContext()->setContextProperty("backend",&c);
        engine.setInitialProperties({{"currentPage","settings"}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        window->resize(960,700);QVERIFY(QTest::qWaitForWindowExposed(window));QTest::qWait(100);
        auto item=[&](const QString& name){return visualItem(window->contentItem(),name);};
        auto click=[&](const QString& name){
            QTest::qWait(30);auto* target=item(name);if(!target||!target->isVisible())return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint());return true;
        };
        auto* scroller=item("navigationScroll");QVERIFY(scroller);
        auto* drawer=window->findChild<QObject*>("sidebarNavigation");QVERIFY(drawer);
        auto count=[&]{return drawer->property("model").value<QJSValue>().toVariant().toList().size();};
        // Fixed destinations must not move when the model viewport reaches either end.
        QStringList fixed{"nav-library","nav-downloaded","nav-model-settings","nav-settings"};
        QMap<QString,qreal> positions;for(const auto& name:fixed){QVERIFY(item(name));positions[name]=item(name)->mapToScene(QPointF()).y();}
        QVERIFY(scroller->mapToScene(QPointF()).y()>=positions["nav-model-settings"]+item("nav-model-settings")->height());
        QVERIFY(scroller->property("contentHeight").toReal()>scroller->height()*2);
        scroller->setProperty("contentY",scroller->property("contentHeight").toReal()-scroller->height());QTest::qWait(60);
        for(const auto& name:fixed)QCOMPARE(item(name)->mapToScene(QPointF()).y(),positions[name]);
        QVERIFY(item("nav-pet:"+ids.last())->mapToScene(QPointF()).y()<positions["nav-settings"]);
        QVERIFY(!item("nav-toggle-pet:"+ids.last())->isVisible());
        // Exercise the real sidebar preference switches, including scroll-to-view.
        auto* panel=item("panelSettingsPage");QVERIFY(panel);
        panel->setProperty("contentY",qMax(0.0,panel->property("contentHeight").toReal()-panel->height()));QTest::qWait(60);
        QVERIFY(click("sidebarQuickAdjustSwitch"));QVERIFY(c.panel()->sidebarQuickAdjust());
        QVERIFY(item("nav-toggle-pet:"+ids.last())->isVisible());
        QVERIFY(std::abs(scroller->property("contentY").toReal()-(scroller->property("contentHeight").toReal()-scroller->height()))<1);
        const QString output=qEnvironmentVariable("ARKPETS_TEST_OUTPUT_DIR");
        if(!output.isEmpty()){QTest::qWait(200);QVERIFY(window->grabWindow().save(output+"/sidebar-long-list.png"));}
        QVERIFY(click("sidebarEnabledOnlySwitch"));QVERIFY(c.panel()->sidebarEnabledOnly());QTest::qWait(60);
        QCOMPARE(count(),3);QVERIFY(!item("nav-pet:"+ids[0]));
        QTRY_VERIFY(std::abs(scroller->property("contentY").toReal())<1);
        // Clicking a trailing switch stops this model without selecting its page.
        QVERIFY(click("nav-toggle-pet:"+ids[3]));QTest::qWait(60);
        QCOMPARE(window->property("currentPage").toString(),QString("settings"));
        QVERIFY(!c.modelSettings(ids[3])["enabled"].toBool());QCOMPARE(count(),2);
        QVERIFY(!item("nav-pet:"+ids[3]));
        c.setPetEnabled(ids[12],false);c.setPetEnabled(ids[22],false);QTest::qWait(60);
        QCOMPARE(count(),0);for(const auto& name:fixed)QCOMPARE(item(name)->mapToScene(QPointF()).y(),positions[name]);
        QVERIFY(click("sidebarEnabledOnlySwitch"));QCOMPARE(count(),24);
        QVERIFY(click("sidebarQuickAdjustSwitch"));QVERIFY(!item("nav-toggle-pet:"+ids[0])->isVisible());
        QCOMPARE(c.preferences()["scale"].toDouble(),1.0);QVERIFY(!c.petRunning());
    }
    void liveSidebarQuickControls() {
        if(qEnvironmentVariableIsEmpty("ARKPETS_SIDEBAR_PROFILE"))QSKIP("Opt-in sidebar controls with two real Hyprland models.");
        const QString profile=qEnvironmentVariable("ARKPETS_SIDEBAR_PROFILE"),output=qEnvironmentVariable("ARKPETS_TEST_OUTPUT_DIR");
        const QString a="2025_shu",b="249_mlyss";
        Controller c(profile+"/data",profile+"/config");QVERIFY2(c.error().isEmpty(),qPrintable(c.error()));
        QCOMPARE(c.pets().size(),2);c.stopPet();
        auto cleanup=qScopeGuard([&]{c.stopPet();});
        c.savePreferences({{"scale",.5},{"fps",30},{"autoActivity",false},{"monitor",""},{"layer","top"}});
        c.panel()->setMode("dark");c.panel()->setPalette("purple");
        c.panel()->setSidebarQuickAdjust(true);c.panel()->setSidebarEnabledOnly(false);
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");engine.rootContext()->setContextProperty("backend",&c);
        engine.setInitialProperties({{"currentPage","model-settings"}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item=[&](const QString& name){return visualItem(window->contentItem(),name);};
        auto click=[&](const QString& name){
            QTest::qWait(40);auto* target=item(name);if(!target||!target->isVisible())return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint());return true;
        };
        auto ready=[&](const QString& id){return c.modelSettings(id)["ready"].toBool();};
        auto pid=[&](const QString& id){return c.modelSettings(id)["pid"].toLongLong();};
        QVERIFY(click("nav-toggle-pet:"+a));QTRY_VERIFY2_WITH_TIMEOUT(ready(a),qPrintable(c.error()),7000);
        qint64 firstA=pid(a);
        QCOMPARE(window->property("currentPage").toString(),QString("model-settings"));
        QVERIFY(click("nav-toggle-pet:"+b));QTRY_VERIFY2_WITH_TIMEOUT(ready(b),qPrintable(c.error()),7000);
        QCOMPARE(pid(a),firstA);qint64 firstB=pid(b);QVERIFY(firstB!=firstA);
        c.panel()->setSidebarEnabledOnly(true);
        QVERIFY(click("nav-toggle-pet:"+a));QTRY_VERIFY_WITH_TIMEOUT(!c.modelSettings(a)["running"].toBool(),4000);
        QVERIFY(!item("nav-pet:"+a));QVERIFY(item("nav-pet:"+b));QCOMPARE(pid(b),firstB);QVERIFY(ready(b));
        QCOMPARE(window->property("currentPage").toString(),QString("model-settings"));
        // A filtered-out pet can still be enabled from the downloaded-model page.
        QVERIFY(click("nav-downloaded"));c.selectModel(a);QVERIFY(click("primaryAction"));
        QTRY_VERIFY2_WITH_TIMEOUT(ready(a),qPrintable(c.error()),7000);
        QVERIFY(item("nav-pet:"+a));QCOMPARE(pid(b),firstB);
        QCOMPARE(window->property("currentPage").toString(),QString("downloaded"));
        QVERIFY(click("nav-pet:"+a));QCOMPARE(window->property("currentPetId").toString(),a);
        if(!output.isEmpty()){QTest::qWait(200);QVERIFY(window->grabWindow().save(output+"/sidebar-individual.png"));}
        QVERIFY(click("nav-settings"));
        if(!output.isEmpty()){QTest::qWait(200);QVERIFY(window->grabWindow().save(output+"/sidebar-settings.png"));}
        firstA=pid(a);c.panel()->setSidebarQuickAdjust(false);QTest::qWait(60);
        QVERIFY(!item("nav-toggle-pet:"+a)->isVisible());QVERIFY(!item("nav-toggle-pet:"+b)->isVisible());
        QCOMPARE(pid(a),firstA);QCOMPARE(pid(b),firstB);QCOMPARE(c.panel()->mode(),QString("dark"));
        c.stopPet();QTRY_VERIFY_WITH_TIMEOUT(!c.petRunning(),4000);
        QVERIFY(!item("nav-pet:"+a)&&!item("nav-pet:"+b));
        c.panel()->setSidebarEnabledOnly(false);
        if(!output.isEmpty()) {
            QFile report(output+"/sidebar-live-report.json");QVERIFY(report.open(QIODevice::WriteOnly));
            report.write(QJsonDocument(QJsonObject{{"result","PASS"},{"platform",QGuiApplication::platformName()},
                {"models",QJsonArray{a,b}},{"quick_switch_starts_and_stops",true},{"other_model_keeps_running",true},
                {"switch_does_not_navigate",true},{"filter_tracks_enabled_state",true},{"restart_from_downloaded_page",true},
                {"hiding_controls_preserves_model_state",true}}).toJson());
        }
    }
    void panelSettingsInteraction() {
        QTemporaryDir dir;
        Controller c(dir.path()+"/data",dir.path()+"/config");
        const auto modelPreferences=c.preferences();
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");
        engine.rootContext()->setContextProperty("backend",&c);
        engine.setInitialProperties({{"currentPage","settings"}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));QTest::qWait(50);
        if (!qEnvironmentVariableIsEmpty("ARKPETS_TEST_OUTPUT_DIR")) window->grabWindow().save(qEnvironmentVariable("ARKPETS_TEST_OUTPUT_DIR")+"/panel-initial.png");
        auto click=[&](const QString& name, qreal fraction=.5) {
            // Changing selection rebuilds segmented delegates; wait for polish.
            QTest::qWait(30);
            auto* item=visualItem(window->contentItem(),name);
            if(!item||!item->isVisible()) return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()*fraction,item->height()/2)).toPoint());
            return true;
        };
        auto* nav=visualItem(window->contentItem(),"nav-settings");QVERIFY(nav);
        QVERIFY(nav->mapToScene(QPointF()).y()>window->height()-100);
        QVERIFY(click("themeModePicker",.5));QCOMPARE(c.panel()->mode(),QString("dark"));QVERIFY(c.panel()->dark());
        const QColor darkBackground=window->color();
        const auto oldScheme=QGuiApplication::styleHints()->colorScheme();
        auto restore=qScopeGuard([&]{QGuiApplication::styleHints()->setColorScheme(oldScheme);});
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);QCoreApplication::processEvents();
        QCOMPARE(window->color(),darkBackground);
        QVERIFY(click("themeModePicker",.1));QCOMPARE(c.panel()->mode(),QString("light"));
        QVERIFY(window->color().lightness()>darkBackground.lightness());
        QVERIFY(click("palette-blue"));QCOMPARE(c.panel()->palette(),QString("blue"));
        QVERIFY(click("palette-monochrome"));QCOMPARE(c.panel()->palette(),QString("monochrome"));
        QVERIFY(click("paletteSourcePicker",.8));QCOMPARE(c.panel()->palette(),QString("matugen"));
        QVERIFY(click("themeModePicker",.9));QCOMPARE(c.panel()->mode(),QString("system"));
        QVERIFY(!window->findChild<QObject*>("wheelRowsField"));
        QCOMPARE(c.preferences(),modelPreferences);QVERIFY(!c.petRunning());
        QVERIFY(click("nav-model-settings"));QCOMPARE(window->property("currentPage").toString(),QString("model-settings"));
        QVERIFY(click("nav-settings"));QCOMPARE(window->property("currentPage").toString(),QString("settings"));
        const QString output=qEnvironmentVariable("ARKPETS_TEST_OUTPUT_DIR");
        if(!output.isEmpty()) {
            QDir().mkpath(output);
            c.panel()->setPalette("blue");c.panel()->setMode("dark");QTest::qWait(120);
            QVERIFY(window->grabWindow().save(output+"/panel-dark.png"));
            c.panel()->setMode("light");QTest::qWait(120);
            QVERIFY(window->grabWindow().save(output+"/panel-light.png"));
            c.panel()->setPalette("matugen");c.panel()->setMode("dark");QTest::qWait(120);
            QVERIFY(window->grabWindow().save(output+"/panel-matugen.png"));
            c.panel()->setPalette("blue");window->resize(960,700);QTest::qWait(120);
            QVERIFY(window->grabWindow().save(output+"/panel-compact.png"));
        }
    }
    void downloadFailureRetryCancelAndOffline() {
        if(qEnvironmentVariableIsEmpty("ARKPETS_TEST_MODEL_DIR"))QSKIP("Set ARKPETS_TEST_MODEL_DIR to a trusted Shu model directory for download integration tests.");
        QTemporaryDir dir;ModelServer server;QVERIFY(server.isListening());
        Controller c(dir.path()+"/data",dir.path()+"/config",server.base());
        QSignalSpy ready(&c,&Controller::catalogReady);c.refreshCatalog();QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,5000);
        QCOMPARE(c.totalCount(),4);QCOMPARE(c.downloadedCount(),0);
        c.selectModel("2025_shu_nian#11");server.failImage=true;c.downloadSelected();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(),5000);QVERIFY(!c.error().isEmpty());QCOMPARE(c.downloadedCount(),0);
        QVERIFY(!QFileInfo::exists(dir.path()+"/data/models/2025_shu_nian#11"));
        QCOMPARE(QDir(dir.path()+"/data/downloads").entryList(QDir::Dirs|QDir::NoDotAndDotDot).size(),0);
        // A leftover incomplete directory must be repaired without manual deletion.
        QDir().mkpath(dir.path()+"/data/models/2025_shu_nian#11");
        QFile partial(dir.path()+"/data/models/2025_shu_nian#11/partial.tmp");
        QVERIFY(partial.open(QIODevice::WriteOnly));partial.write("interrupted");partial.close();
        server.failImage=false;QSignalSpy completed(&c,&Controller::downloadFinished);c.downloadSelected();
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(),1,10000);QCOMPARE(c.downloadedCount(),1);QVERIFY(c.error().isEmpty());
        QVERIFY(c.animations().contains("Relax"));QVERIFY(c.selected()["downloaded"].toBool());
        QVERIFY(!QFileInfo::exists(partial.fileName()));
        QCOMPARE(QDir(dir.path()+"/data/downloads").entryList(QDir::Dirs|QDir::NoDotAndDotDot).size(),0);
        QVERIFY(server.requests.join(' ').contains("%23"));
        auto metadata=QJsonDocument::fromJson(readFile(dir.path()+"/data/models/2025_shu_nian#11/complete.json")).object();
        QCOMPARE(metadata["sha256"].toObject().size(),3);
        Controller offline(dir.path()+"/data",dir.path()+"/config",QUrl("http://127.0.0.1:1/"));
        QCOMPARE(offline.totalCount(),4);QCOMPARE(offline.downloadedCount(),1);QVERIFY(offline.animations().contains("Relax"));
        c.selectModel("2025_shu");server.stall=true;c.downloadSelected();c.cancelDownload();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(),5000);QCOMPARE(c.downloadedCount(),1);
        QCOMPARE(QDir(dir.path()+"/data/downloads").entryList(QDir::Dirs|QDir::NoDotAndDotDot).size(),0);
        c.selectModel("2025_shu_nian#11");c.removeSelected();QCOMPARE(c.downloadedCount(),0);
    }
    void liveAnimationSelectionDisablesActivity() {
        if(qEnvironmentVariableIsEmpty("ARKPETS_ANIMATION_PROFILE"))QSKIP("Opt-in animation selection with two real Hyprland models.");
        const QString profile=qEnvironmentVariable("ARKPETS_ANIMATION_PROFILE"),output=qEnvironmentVariable("ARKPETS_TEST_OUTPUT_DIR");
        const QString a="171_bldsk_witch#1",b="2025_shu";
        Controller c(profile+"/data",profile+"/config");QVERIFY2(c.error().isEmpty(),qPrintable(c.error()));
        c.stopPet();auto cleanup=qScopeGuard([&]{c.stopPet();});
        c.savePreferences({{"scale",.5},{"fps",30},{"autoActivity",true},{"monitor",""},{"layer","top"}});
        for(const auto& id:{a,b})c.saveModelSettings(id,{{"multiplier",1.0},{"overrides",QVariantMap{}},{"animation","Relax"}});
        c.panel()->setMode("dark");c.panel()->setPalette("purple");
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");engine.rootContext()->setContextProperty("backend",&c);
        engine.setInitialProperties({{"currentPage","pet:"+a}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* picker=window->findChild<QQuickItem*>("individualAnimationPicker");QVERIFY(picker&&picker->isEnabled());
        auto* activity=window->findChild<QObject*>("individualActivityPicker");QVERIFY(activity);
        QVERIFY(!c.modelSettings(a)["overrides"].toMap().contains("autoActivity"));
        auto click=[&](const QString& name){
            QTest::qWait(40);auto* target=visualItem(window->contentItem(),name);if(!target||!target->isVisible())return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint());return true;
        };
        auto choose=[&](const QString& name,const QString& label){
            if(!click(name))return false;QTest::qWait(220);
            QQuickItem* option=nullptr;
            std::function<void(QQuickItem*)> visit=[&](QQuickItem* item){
                if(item->isVisible()&&item->property("text").toString()==label)option=item;
                for(auto* child:item->childItems())visit(child);
            };
            visit(window->contentItem());if(!option)return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,option->mapToScene(QPointF(option->width()/2,option->height()/2)).toPoint());
            auto* combo=window->findChild<QObject*>(name);
            return combo&&QTest::qWaitFor([&]{return !combo->property("menuOpen").toBool();},1500);
        };
        auto ready=[&](const QString& id){return c.modelSettings(id)["ready"].toBool();};
        auto pid=[&](const QString& id){return c.modelSettings(id)["pid"].toLongLong();};
        c.setPetEnabled(a,true);c.setPetEnabled(b,true);QTRY_VERIFY2_WITH_TIMEOUT(ready(a)&&ready(b),qPrintable(c.error()),7000);
        qint64 firstA=pid(a),firstB=pid(b);
        QVERIFY(choose("individualAnimationPicker","Sit"));QCOMPARE(activity->property("currentIndex").toInt(),2);
        // Selection edits the draft; discarding it preserves inherited activity.
        QVERIFY(c.modelSettings(a)["effective"].toMap()["autoActivity"].toBool());QCOMPARE(pid(a),firstA);
        QVERIFY(click("discardIndividualSettings"));QCOMPARE(picker->property("currentText").toString(),QString("Relax"));
        QCOMPARE(activity->property("currentIndex").toInt(),0);QVERIFY(picker->isEnabled());
        // Choosing the already-selected Relax is still an explicit manual choice.
        QVERIFY(choose("individualAnimationPicker","Relax"));QCOMPARE(activity->property("currentIndex").toInt(),2);
        QVERIFY(click("saveIndividualSettings"));QTRY_VERIFY_WITH_TIMEOUT(ready(a)&&pid(a)!=firstA,7000);
        QVERIFY(!c.modelSettings(a)["effective"].toMap()["autoActivity"].toBool());
        QVERIFY(c.preferences()["autoActivity"].toBool());QCOMPARE(pid(b),firstB);QVERIFY(ready(b));
        firstA=pid(a);
        QVERIFY(choose("individualActivityPicker","开启"));QVERIFY(click("saveIndividualSettings"));
        QTRY_VERIFY_WITH_TIMEOUT(ready(a)&&pid(a)!=firstA,7000);QVERIFY(picker->isEnabled());
        firstA=pid(a);
        QVERIFY(choose("individualAnimationPicker","Special"));QCOMPARE(activity->property("currentIndex").toInt(),2);
        QVERIFY(click("saveIndividualSettings"));QTRY_VERIFY_WITH_TIMEOUT(ready(a)&&pid(a)!=firstA,7000);
        auto args=readFile("/proc/"+QString::number(pid(a))+"/cmdline").split('\0');
        QVERIFY(!args.contains("--auto-activity"));QCOMPARE(args.value(args.indexOf("--animation")+1),QByteArray("Special"));
        QCOMPARE(pid(b),firstB);QVERIFY(ready(b));
        if(!output.isEmpty()){QTest::qWait(150);QVERIFY(window->grabWindow().save(output+"/animation-selection.png"));}
        c.stopPet();QTRY_VERIFY_WITH_TIMEOUT(!c.petRunning(),4000);
        Controller restored(profile+"/data",profile+"/config");
        QCOMPARE(restored.modelSettings(a)["animation"].toString(),QString("Special"));
        QVERIFY(!restored.modelSettings(a)["effective"].toMap()["autoActivity"].toBool());
        QVERIFY(restored.preferences()["autoActivity"].toBool());
        if(!output.isEmpty()) {
            QFile report(output+"/animation-selection-report.json");QVERIFY(report.open(QIODevice::WriteOnly));
            report.write(QJsonDocument(QJsonObject{{"result","PASS"},{"inherited_and_explicit_auto_disabled",true},
                {"selecting_current_animation_enters_manual_mode",true},{"discard_preserves_inheritance",true},
                {"saved_animation_restored",true},{"other_model_not_restarted",true}}).toJson());
        }
    }
    void liveMultipleModels() {
        if (qEnvironmentVariableIsEmpty("ARKPETS_MULTI_PROFILE")) QSKIP("Opt-in multiple-model Hyprland integration test.");
        const QString profile=qEnvironmentVariable("ARKPETS_MULTI_PROFILE"),output=qEnvironmentVariable("ARKPETS_TEST_OUTPUT_DIR");
        const QString a="2025_shu",b="249_mlyss";
        QJsonObject report;
        QString namespaceA,namespaceB;
        auto hypr=[](const QString& command) {
            QProcess process;process.start("hyprctl",{"-j",command});process.waitForFinished(2000);
            return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        };
        auto layer=[&](const QString& name) {
            for (const auto& monitor:hypr("layers")) for (const auto& level:monitor.toObject()["levels"].toObject())
                for (const auto& entry:level.toArray()) if (entry.toObject()["namespace"].toString()==name) return entry.toObject();
            return QJsonObject();
        };
        auto arguments=[](qint64 pid) {return readFile("/proc/"+QString::number(pid)+"/cmdline").split('\0');};
        auto argument=[&](qint64 pid,const QByteArray& key) {auto args=arguments(pid);return args.value(args.indexOf(key)+1);};
        {
            Controller c(profile+"/data",profile+"/config");
            QVERIFY2(c.error().isEmpty(),qPrintable(c.error()));QCOMPARE(c.pets().size(),2);
            c.stopPet();
            c.savePreferences({{"scale",.8},{"fps",30},{"autoActivity",false},{"monitor","DP-4"},{"layer","overlay"},{"clickThrough",false},{"flip",false}});
            c.saveModelSettings(a,{{"multiplier",1.0},{"overrides",QVariantMap{}}});
            c.saveModelSettings(b,{{"multiplier",.75},{"overrides",QVariantMap{{"flip",true}}}});
            namespaceA=c.modelSettings(a)["namespace"].toString();namespaceB=c.modelSettings(b)["namespace"].toString();
            QVERIFY(namespaceA!=namespaceB);
            auto pid=[&](const QString& id){return c.modelSettings(id)["pid"].toLongLong();};
            auto ready=[&](const QString& id){return c.modelSettings(id)["ready"].toBool();};
            QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");
            engine.rootContext()->setContextProperty("backend",&c);
            engine.setInitialProperties({{"currentPage","pet:"+a}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
            QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
            QVERIFY(QTest::qWaitForWindowExposed(window));QTest::qWait(150);
            auto click=[&](const QString& name) {
                auto* item=visualItem(window->contentItem(),name);if(!item||!item->isVisible())return false;
                QTest::qWait(30);
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;
            };
            auto choose=[&](const QString& name,const QString& label) {
                if(!click(name))return false;QTest::qWait(220);
                QQuickItem* option=nullptr;
                std::function<void(QQuickItem*)> visit=[&](QQuickItem* item){
                    if(item->isVisible()&&item->property("text").toString()==label)option=item;
                    for(auto* child:item->childItems())visit(child);
                };
                visit(window->contentItem());if(!option)return false;
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,option->mapToScene(QPointF(option->width()/2,option->height()/2)).toPoint());
                auto* combo=window->findChild<QObject*>(name);
                return combo&&QTest::qWaitFor([&]{return !combo->property("menuOpen").toBool();},1500);
            };
            QCOMPARE(window->findChild<QObject*>("individualModelName")->property("text").toString(),QString("黍"));
            auto* individual=window->findChild<QObject*>("individualSettings");QVERIFY(individual);
            QVERIFY(!individual->findChild<QObject*>("fpsField"));
            auto* multiplier=window->findChild<QObject*>("multiplierField");QVERIFY(multiplier);
            auto* input=qvariant_cast<QQuickItem*>(multiplier->property("input"));QVERIFY(input);
            input->forceActiveFocus();QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
            for (auto key:{Qt::Key_1,Qt::Key_Period,Qt::Key_2,Qt::Key_5}) QTest::keyClick(window,key);
            QVERIFY(click("saveIndividualSettings"));QCOMPARE(c.modelSettings(a)["multiplier"].toDouble(),1.25);
            QVERIFY(choose("individualActivityPicker","关闭"));
            QVERIFY(choose("individualMonitorPicker","DP-4"));
            QVERIFY(click("saveIndividualSettings"));QVERIFY(!c.modelSettings(a)["overrides"].toMap()["autoActivity"].toBool());
            QVERIFY(click("modelEnabledSwitch"));QTRY_VERIFY2_WITH_TIMEOUT(ready(a),qPrintable(c.error()),7000);
            qint64 firstA=pid(a);
            QVERIFY(click("nav-pet:"+b));QCOMPARE(window->property("currentPetId").toString(),b);
            QVERIFY(click("modelEnabledSwitch"));QTRY_VERIFY2_WITH_TIMEOUT(ready(b),qPrintable(c.error()),7000);
            QCOMPARE(pid(a),firstA);QVERIFY(pid(a)!=pid(b));
            QCOMPARE(argument(pid(a),"--scale"),QByteArray("1"));QCOMPARE(argument(pid(b),"--scale"),QByteArray("0.6"));
            QVERIFY(!layer(namespaceA).isEmpty()&&!layer(namespaceB).isEmpty());
            const qint64 firstB=pid(b);
            QVERIFY(click("nav-pet:"+a));
            if(!output.isEmpty()) {QTest::qWait(100);QVERIFY(window->grabWindow().save(output+"/individual-settings.png"));}
            // Disable one model through the actual MD3 switch; the other process
            // and its layer must survive, including the shared compositor rule.
            QVERIFY(click("modelEnabledSwitch"));QTRY_VERIFY_WITH_TIMEOUT(!c.modelSettings(a)["running"].toBool(),4000);
            QCOMPARE(pid(b),firstB);QVERIFY(ready(b));QVERIFY(!layer(namespaceB).isEmpty());
            QVERIFY(click("modelEnabledSwitch"));QTRY_VERIFY_WITH_TIMEOUT(ready(a),7000);
            // A native drag reports only this model's new position.
            auto before=layer(namespaceA),other=layer(namespaceB),cursor=hypr("cursorpos");
            const int initialRight=c.modelSettings(a)["effective"].toMap()["right"].toInt();
            auto move=[](int x,int y){QProcess p;p.start("hyprctl",{"eval",QString("hl.dispatch(hl.dsp.cursor.move({x=%1,y=%2}))").arg(x).arg(y)});p.waitForFinished(1000);};
            auto restoreCursor=qScopeGuard([&]{move(cursor["x"].toInt(),cursor["y"].toInt());});
            window->hide();QTest::qWait(150);
            move(before["x"].toInt()+int(before["w"].toInt()*.5),before["y"].toInt()+int(before["h"].toInt()*.4));
            QProcess pointer;pointer.start(QCoreApplication::applicationDirPath()+"/drag-probe",{"-90","-60"});
            QVERIFY(pointer.waitForFinished(5000));QCOMPARE(pointer.exitCode(),0);
            QTRY_VERIFY_WITH_TIMEOUT(layer(namespaceA)["x"].toInt()<before["x"].toInt()-60,2500);
            QCOMPARE(layer(namespaceB)["x"],other["x"]);QCOMPARE(pid(b),firstB);
            QTRY_VERIFY_WITH_TIMEOUT(c.modelSettings(a)["effective"].toMap()["right"].toInt()>initialRight+60,2000);
            report["drag_isolated"]=true;window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
            // General scale/FPS reach both models, applying each saved multiplier.
            firstA=pid(a);
            c.savePreferences({{"scale",1.0},{"fps",48}});
            QTRY_VERIFY_WITH_TIMEOUT(ready(a)&&ready(b)&&pid(a)!=firstA&&pid(b)!=firstB,8000);
            QCOMPARE(argument(pid(a),"--scale"),QByteArray("1.25"));QCOMPARE(argument(pid(b),"--scale"),QByteArray("0.75"));
            QCOMPARE(argument(pid(a),"--fps"),QByteArray("48"));QCOMPARE(argument(pid(b),"--fps"),QByteArray("48"));
            firstA=pid(a);qint64 secondB=pid(b);
            c.savePreferences({{"autoActivity",true}});
            QTRY_VERIFY_WITH_TIMEOUT(ready(b)&&pid(b)!=secondB,7000);
            QCOMPARE(pid(a),firstA);QVERIFY(!arguments(pid(a)).contains("--auto-activity"));
            QVERIFY(arguments(pid(b)).contains("--auto-activity"));
            if(c.monitors().contains("DP-3")) {
                secondB=pid(b);c.savePreferences({{"monitor","DP-3"}});
                QTRY_VERIFY_WITH_TIMEOUT(ready(b)&&pid(b)!=secondB,7000);
                QCOMPARE(pid(a),firstA);QCOMPARE(argument(pid(a),"--monitor"),QByteArray("DP-4"));
                QCOMPARE(argument(pid(b),"--monitor"),QByteArray("DP-3"));
            }
            // An unexpected exit disables only the failed model.
            secondB=pid(b);QCOMPARE(QProcess::execute("kill",{"-KILL",QString::number(pid(a))}),0);
            QTRY_VERIFY_WITH_TIMEOUT(!c.modelSettings(a)["enabled"].toBool(),5000);
            QCOMPARE(pid(b),secondB);QVERIFY(ready(b));QVERIFY(!c.modelSettings(a)["error"].toString().isEmpty());
            c.clearError();
            report["independent_processes_and_failure"]=true;report["global_scale_and_fps"]=true;
            report["overrides_and_inheritance"]=true;
            window->setProperty("currentPage","model-settings");QTest::qWait(120);
            if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/general-settings.png"));
            // Leave only B enabled for the restart/persistence check below.
        }
        QTRY_VERIFY_WITH_TIMEOUT(layer(namespaceA).isEmpty()&&layer(namespaceB).isEmpty(),3000);
        {
            Controller restored(profile+"/data",profile+"/config");restored.restorePets();
            QTRY_VERIFY_WITH_TIMEOUT(restored.modelSettings(b)["ready"].toBool(),7000);
            QVERIFY(!restored.modelSettings(a)["running"].toBool());
            QCOMPARE(restored.modelSettings(a)["multiplier"].toDouble(),1.25);
            const qint64 survivor=restored.modelSettings(b)["pid"].toLongLong();
            // A rapid off/on while termination is pending must launch exactly once.
            restored.setPetEnabled(a,true);restored.setPetEnabled(a,false);restored.setPetEnabled(a,true);
            QTRY_VERIFY_WITH_TIMEOUT(restored.modelSettings(a)["ready"].toBool(),8000);
            QCOMPARE(restored.modelSettings(b)["pid"].toLongLong(),survivor);
            QVERIFY(!layer(namespaceA).isEmpty()&&!layer(namespaceB).isEmpty());
            restored.stopPet();QTRY_VERIFY_WITH_TIMEOUT(!restored.petRunning(),5000);
            QVERIFY(!restored.modelSettings(a)["enabled"].toBool()&&!restored.modelSettings(b)["enabled"].toBool());
        }
        QTRY_VERIFY_WITH_TIMEOUT(layer(namespaceA).isEmpty()&&layer(namespaceB).isEmpty(),3000);
        report["enabled_state_restored"]=true;report["rapid_toggle"]=true;report["result"]="PASS";
        if(!output.isEmpty()) {QFile f(output+"/multiple-models-report.json");QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(report).toJson());}
    }
    void liveRendererStartupFailure() {
        if(!qEnvironmentVariableIsSet("ARKPETS_LIVE_QA"))QSKIP("Opt-in Hyprland renderer failure test.");
        QString profile=qEnvironmentVariable("ARKPETS_TEST_PROFILE");QVERIFY(!profile.isEmpty());
        QTemporaryDir config;Controller c(profile+"/data",config.path());
        c.selectModel("2025_shu");QVERIFY(c.selected()["downloaded"].toBool());
        auto old=qgetenv("GDK_DISABLE");
        auto restore=qScopeGuard([old] {if(old.isNull())qunsetenv("GDK_DISABLE");else qputenv("GDK_DISABLE",old);});
        qputenv("GDK_DISABLE","gl");
        bool announcedReady=false;
        connect(&c,&Controller::activityChanged,this,[&] {if(c.status().contains("已显示到桌面"))announcedReady=true;});
        c.startPet("Relax");
        QTRY_VERIFY_WITH_TIMEOUT(!c.error().isEmpty(),6000);
        QTRY_VERIFY_WITH_TIMEOUT(!c.petRunning(),3000);
        QVERIFY2(c.error().contains("GL",Qt::CaseInsensitive),qPrintable(c.error()));
        QVERIFY2(c.error().contains("退出码 1"),qPrintable(c.error()));
        QVERIFY(!announcedReady);
    }
    void liveGuiDownloadSettingsAndDrag() {
        if(!qEnvironmentVariableIsSet("ARKPETS_LIVE_QA"))QSKIP("Opt-in live network/Hyprland GUI integration test.");
        QString profile=qEnvironmentVariable("ARKPETS_TEST_PROFILE");QVERIFY(!profile.isEmpty());
        Controller c(profile+"/data",profile+"/config");
        if(!c.totalCount()) {QSignalSpy ready(&c,&Controller::catalogReady);c.refreshCatalog();QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,45000);}
        QVERIFY2(c.totalCount()>1000,qPrintable(c.error()));
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");engine.rootContext()->setContextProperty("backend",&c);engine.rootContext()->setContextProperty("trayAvailable",false);
        engine.setInitialProperties({{"initialSearch","黍"}});engine.load(QUrl("qrc:/ui/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));QTest::qWait(120);
        auto click=[window](const char* name) {
            auto* item=window->findChild<QQuickItem*>(name);
            if(!item)return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;
        };
        QTRY_COMPARE_WITH_TIMEOUT(c.visibleCount(),2,3000);c.selectModel("2025_shu");
        if(!c.selected()["downloaded"].toBool()) {
            QSignalSpy completed(&c,&Controller::downloadFinished);
            QVERIFY(click("primaryAction"));QTRY_COMPARE_WITH_TIMEOUT(completed.count(),1,90000);
        }
        QVERIFY2(c.selected()["downloaded"].toBool(),qPrintable(c.error()));QVERIFY(c.animations().contains("Relax"));
        c.savePreferences({{"scale",.7},{"fps",30},{"autoActivity",false},{"monitor","DP-4"},{"layer","top"},{"clickThrough",false},{"flip",false}});
        c.saveModelSettings("2025_shu",{{"multiplier",1.0},{"overrides",QVariantMap{}}});
        c.resetPosition();
        QVERIFY(click("primaryAction"));QTRY_VERIFY2_WITH_TIMEOUT(c.status().contains("已显示到桌面"),qPrintable(c.error()+" / "+c.status()),6000);
        QVERIFY(click("hideToTrayButton"));QVERIFY(!window->isVisible());
        QTest::qWait(300);QVERIFY(c.petRunning());
        window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        window->setProperty("currentPage","model-settings");QTest::qWait(100);
        window->setProperty("draftScale",.86);window->setProperty("draftFps",48);
        QVERIFY(click("saveSettings"));QTRY_VERIFY_WITH_TIMEOUT(c.status().contains("已显示到桌面"),6000);
        QCOMPARE(c.preferences()["scale"].toDouble(),.86);QCOMPARE(c.preferences()["fps"].toInt(),48);
        auto hypr=[](const QString& cmd) {
            QProcess p;p.start("hyprctl",{"-j",cmd});p.waitForFinished(2000);return QJsonDocument::fromJson(p.readAllStandardOutput()).object();
        };
        auto pet=[hypr,&c] {
            auto layers=hypr("layers");
            for(auto m:layers)for(auto level:m.toObject()["levels"].toObject())for(auto entry:level.toArray())
                if(entry.toObject()["namespace"].toString()==c.modelSettings("2025_shu")["namespace"].toString())return entry.toObject();
            return QJsonObject();
        };
        QTRY_VERIFY_WITH_TIMEOUT(pet()["w"].toInt()>0&&pet()["h"].toInt()>344,3000);QTest::qWait(400);
        auto before=pet(),cursor=hypr("cursorpos");
        auto move=[](int x,int y){QProcess p;p.start("hyprctl",{"eval",QString("hl.dispatch(hl.dsp.cursor.move({x=%1,y=%2}))").arg(x).arg(y)});p.waitForFinished(1000);};
        move(before["x"].toInt()+int(before["w"].toInt()*.55),before["y"].toInt()+int(before["h"].toInt()*.45));QTest::qWait(100);
        QProcess pointer;pointer.start(QCoreApplication::applicationDirPath()+"/drag-probe",{"-90","-60"});
        QVERIFY(pointer.waitForFinished(5000));QCOMPARE(pointer.exitCode(),0);QTest::qWait(300);
        auto after=pet();
        move(cursor["x"].toInt(),cursor["y"].toInt());
        QVERIFY2(after["x"].toInt()<before["x"].toInt()-30,qPrintable(QString::fromUtf8(QJsonDocument(after).toJson())));
        QVERIFY(after["y"].toInt()<before["y"].toInt()-20);
        QVERIFY(c.modelSettings("2025_shu")["effective"].toMap()["right"].toInt()>64);QVERIFY(c.modelSettings("2025_shu")["effective"].toMap()["bottom"].toInt()>40);
        {Controller persisted(profile+"/data",profile+"/config");
            QCOMPARE(persisted.modelSettings("2025_shu")["effective"].toMap()["right"],c.modelSettings("2025_shu")["effective"].toMap()["right"]);
            QCOMPARE(persisted.modelSettings("2025_shu")["effective"].toMap()["bottom"],c.modelSettings("2025_shu")["effective"].toMap()["bottom"]);}
        // Use the actual MD3 switch and Save button to enable automatic movement.
        auto* automatic=window->findChild<QObject*>("autoActivitySwitch");QVERIFY(automatic);
        QVERIFY(click("autoActivitySwitch"));QVERIFY(window->property("draftAutoActivity").toBool());
        QVERIFY(click("saveSettings"));QTRY_VERIFY_WITH_TIMEOUT(c.status().contains("已显示到桌面"),6000);
        QTRY_VERIFY_WITH_TIMEOUT(pet()["pid"]!=after["pid"],3000);
        auto autoStart=pet();
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(pet()["x"].toInt()-autoStart["x"].toInt())>25,6500);
        auto autoMoved=pet();
        QString output=qEnvironmentVariable("ARKPETS_TEST_OUTPUT_DIR");
        if(!output.isEmpty()) {
            QDir().mkpath(output);QTest::qWait(100);QVERIFY(window->grabWindow().save(output+"/settings.png"));
            window->setProperty("currentPage","library");QTest::qWait(600);QVERIFY(window->grabWindow().save(output+"/model-library.png"));
            QJsonObject report{{"live_index_models",c.totalCount()},{"downloaded",c.downloadedCount()},{"drag_before",before},{"drag_after",after},
                {"scale",c.preferences()["scale"].toDouble()},{"fps",c.preferences()["fps"].toInt()},{"saved_right",c.modelSettings("2025_shu")["effective"].toMap()["right"].toInt()},{"saved_bottom",c.modelSettings("2025_shu")["effective"].toMap()["bottom"].toInt()},
                {"auto_activity_start",autoStart},{"auto_activity_moved",autoMoved},{"component_library","sudoevolve/material-components-qml"}};
            QFile file(output+"/integration-report.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(report).toJson());
        }
        window->setProperty("currentPage","model-settings");QTest::qWait(50);
        QVERIFY(click("autoActivitySwitch"));QVERIFY(!window->property("draftAutoActivity").toBool());
        QVERIFY(click("saveSettings"));QTRY_VERIFY_WITH_TIMEOUT(c.status().contains("已显示到桌面"),6000);
        QTRY_VERIFY_WITH_TIMEOUT(pet()["pid"]!=autoMoved["pid"],3000);
        QTest::qWait(150);auto stationary=pet();QTest::qWait(4200);
        QCOMPARE(pet()["x"],stationary["x"]);QVERIFY(!c.preferences()["autoActivity"].toBool());
        // A final position report during renderer shutdown must not undo Reset.
        c.resetPosition();QTRY_VERIFY_WITH_TIMEOUT(pet()["pid"]!=stationary["pid"],3000);
        QTRY_COMPARE_WITH_TIMEOUT(c.modelSettings("2025_shu")["effective"].toMap()["right"].toInt(),64,3000);
        QCOMPARE(c.modelSettings("2025_shu")["effective"].toMap()["bottom"].toInt(),40);
        c.stopPet();QTRY_VERIFY_WITH_TIMEOUT(!c.petRunning(),5000);
    }
};
int main(int argc,char** argv) {qmlRegisterType<ListWheelHandler>("ArkPets",1,0,"ListWheelHandler");QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);BackendTest test;return QTest::qExec(&test,argc,argv);}
#include "backend_test.moc"
