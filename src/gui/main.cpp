#include "controller.h"
#include "listwheelhandler.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QImageReader>
#include <iostream>

class ScrollTrace : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Wheel) {
            auto* wheel = static_cast<QWheelEvent*>(event);
            qInfo() << "ArkPets window wheel" << wheel->position() << wheel->angleDelta()
                    << wheel->pixelDelta() << "phase" << wheel->phase()
                    << "device" << wheel->pointingDevice()->type()
                    << "source" << wheel->source() << "buttons" << wheel->buttons();
            if (auto* handler = watched->findChild<QQuickItem*>("modelWheelHandler"))
                qInfo() << "ArkPets handler" << handler->mapToScene(QPointF()) << handler->size()
                        << "visible" << handler->isVisible() << "step" << handler->property("step");
        }
        return false;
    }
};

int main(int argc,char** argv) {
    if (qEnvironmentVariableIsSet("ARKPETS_TRACE_SCROLL")) qputenv("QT_FORCE_STDERR_LOGGING", "1");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<ListWheelHandler>("ArkPets", 1, 0, "ListWheelHandler");
    QApplication app(argc,argv);
    app.setApplicationName("ArkPets"); app.setOrganizationName("ArkPets");
    app.setApplicationVersion("0.9.10"); app.setDesktopFileName("arkpets");
    QImageReader::setAllocationLimit(128);
    QCommandLineParser parser;parser.setApplicationDescription("ArkPets Linux · Material 桌宠管理器");
    parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"data-dir","模型与索引目录（默认使用 XDG 用户数据目录）","directory"});
    parser.addOption({"config-dir","设置目录（默认使用 XDG 用户配置目录）","directory"});
    parser.addOption({"stop","停止运行中的桌宠，保留管理界面"});
    parser.addOption({"quit","退出运行中的管理器与桌宠"});
    parser.addOption({"screenshot","保存界面截图，用于验证","file"});
    parser.addOption({"page","初始页面：library / downloaded / model-settings / settings","name","library"});
    parser.addOption({"search","初始搜索词","text"});
    parser.addOption({"exit-after","指定毫秒后退出，用于验证","milliseconds"});
    parser.process(app);
    QString root=parser.value("data-dir");
    if (root.isEmpty()) root=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/arkpets";
    root=QDir(root).absolutePath();
    if (!QDir().mkpath(root)) {std::cerr<<"无法创建数据目录\n";return 1;}
    QString socketPath=root+"/control.sock";
    QLocalSocket client;client.connectToServer(socketPath);
    if (client.waitForConnected(150)) {
        client.write(parser.isSet("quit")?"quit\n":parser.isSet("stop")?"stop\n":"show\n");
        client.flush();client.waitForBytesWritten(300);return 0;
    }
    if (parser.isSet("stop")||parser.isSet("quit")) return 0;
    QLockFile lock(root+"/app.lock");
    if (!lock.tryLock(100)) {std::cerr<<"ArkPets 已在运行，或数据目录被其他实例占用。\n";return 1;}
    QLocalServer::removeServer(socketPath);
    QLocalServer server;server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(socketPath)) {std::cerr<<server.errorString().toStdString()<<'\n';return 1;}
    Controller backend(root,parser.value("config-dir"));
    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/qt/qml");
    bool trayAvailable=QSystemTrayIcon::isSystemTrayAvailable();
    engine.rootContext()->setContextProperty("backend",&backend);
    engine.rootContext()->setContextProperty("trayAvailable",trayAvailable);
    engine.setInitialProperties({{"currentPage",parser.value("page")},{"initialSearch",parser.value("search")}});
    engine.load(QUrl("qrc:/ui/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return 1;
    if (qEnvironmentVariableIsSet("ARKPETS_TRACE_SCROLL")) {
        window->setTitle("ArkPets · 滚轮诊断");
        window->installEventFilter(new ScrollTrace(window));
        qInfo() << "ArkPets scroll trace ready" << app.applicationVersion();
    }
    auto show=[window] {window->show();window->raise();window->requestActivate();};
    QPixmap icon(64,64);icon.fill(Qt::transparent);
    {QPainter p(&icon);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(QColor("#006b58"));
        p.drawRoundedRect(QRectF(2,2,60,60),18,18);p.setBrush(QColor("#b8f4d5"));
        p.drawEllipse(QRectF(18,30,29,21));p.drawEllipse(QRectF(13,19,11,14));
        p.drawEllipse(QRectF(27,12,11,15));p.drawEllipse(QRectF(41,19,11,14));}
    app.setWindowIcon(QIcon(icon));
    QSystemTrayIcon tray{QIcon(icon)};
    QMenu menu;
    menu.addAction("打开 ArkPets",&app,show);
    menu.addAction("停止全部桌宠",&backend,&Controller::stopPet);
    menu.addSeparator();menu.addAction("退出",&app,&QApplication::quit);
    tray.setToolTip("ArkPets");tray.setContextMenu(&menu);
    QObject::connect(&tray,&QSystemTrayIcon::activated,&app,[show](QSystemTrayIcon::ActivationReason reason) {
        if (reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick) show();
    });
    // A missing or restarting tray host must never turn Hide into Quit.
    app.setQuitOnLastWindowClosed(false);
    tray.show();
    QObject::connect(&server,&QLocalServer::newConnection,&app,[&] {
        while (auto* socket=server.nextPendingConnection()) {
            auto read=[socket,&backend,&app,show] {
                QByteArray line=socket->readAll().trimmed();
                if (line=="quit") app.quit();else if (line=="stop") backend.stopPet();else if (line=="show") show();
                socket->disconnectFromServer();
            };
            QObject::connect(socket,&QLocalSocket::readyRead,&app,read);
            QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            if (socket->bytesAvailable()) read();
        }
    });
    QTimer::singleShot(0,&backend,&Controller::restorePets);
    QObject::connect(&backend,&Controller::catalogReady,&backend,&Controller::restorePets);
    if (!backend.totalCount()) QTimer::singleShot(0,&backend,&Controller::refreshCatalog);
    if (parser.isSet("screenshot")) QTimer::singleShot(1200,window,[window,file=parser.value("screenshot")] {
        if (!window->grabWindow().save(file)) qWarning("界面截图保存失败");
    });
    if (parser.isSet("exit-after")) {
        bool valid=false;int timeout=parser.value("exit-after").toInt(&valid);
        if (valid && timeout>0) QTimer::singleShot(timeout,&app,&QApplication::quit);
    }
    return app.exec();
}
