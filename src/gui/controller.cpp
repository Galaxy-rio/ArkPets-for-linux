#include "controller.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QUuid>
#include <memory>
#include <cmath>
#include <algorithm>

static QString dataRoot(QString supplied) {
    return supplied.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/arkpets" : supplied;
}
static QString configFile(QString supplied) {
    QString root = supplied.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/arkpets" : supplied;
    QDir().mkpath(root); return root + "/settings.ini";
}
static bool saveBytes(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

Controller::Controller(QString root, QString config, QUrl base, QObject* parent)
    : QObject(parent), m_root(dataRoot(std::move(root))), m_settings(configFile(config), QSettings::IniFormat), m_panel(config),
      m_catalog(m_root, this), m_filter(this), m_network(this),
      m_base(base.isEmpty() ? QUrl("https://raw.githubusercontent.com/isHarryh/Ark-Models/main/") : base) {
    if (!QDir().mkpath(m_root + "/models")) m_error = "无法创建模型保存目录：" + m_root;
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    // Respect the user's usual HTTPS proxy, including when running in a development sandbox.
    QString proxy = qEnvironmentVariable("https_proxy", qEnvironmentVariable("HTTPS_PROXY"));
    if (!proxy.isEmpty() && m_base.host() != "127.0.0.1" && m_base.host() != "localhost") {
        QUrl url(proxy);
        if (!url.host().isEmpty()) m_network.setProxy(QNetworkProxy(
            url.scheme().startsWith("socks") ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy,
            url.host(), url.port(8080), url.userName(), url.password()));
    }
    m_filter.setSourceModel(&m_catalog); m_filter.setQuery({}, "Operator", false);
    connect(&m_catalog, &CatalogModel::countsChanged, this, [this] { emit catalogChanged(); notifyPets(); });
    connect(&m_filter, &QAbstractItemModel::modelReset, this, &Controller::catalogChanged);
    connect(&m_filter, &QAbstractItemModel::rowsInserted, this, [this] { emit catalogChanged(); });
    connect(&m_filter, &QAbstractItemModel::rowsRemoved, this, [this] { emit catalogChanged(); });
    QFile cache(m_root + "/catalog.json");
    QString error;
    if (cache.open(QIODevice::ReadOnly)) m_catalog.load(cache.readAll(), &error);
    m_selectedId = m_settings.value("lastModel", "2025_shu").toString();
    refreshMonitors();
    if (m_catalog.find(m_selectedId)) loadAnimations();
    m_profileTag = QString::fromLatin1(QCryptographicHash::hash(QDir(m_root).absolutePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
    connect(&m_windowTracker,&WindowTracker::updated,this,&Controller::sendWindowEdges);
    // Keep the previous single pet's saved position, without applying it to all
    // future models. Existing display/behaviour values become general defaults.
    if (m_settings.value("petsSchema",0).toInt() < 1) {
        if (m_settings.contains("right") || m_settings.contains("bottom")) {
            const QString prefix=settingsPrefix(m_selectedId);
            m_settings.setValue(prefix+"right",m_settings.value("right",64));
            m_settings.setValue(prefix+"bottom",m_settings.value("bottom",40));
        }
        m_settings.setValue("petsSchema",1);m_settings.sync();
    }
    // A per-model Relax height cannot be converted into one shared raw scale.
    // Retain relative multipliers and all other preferences when retiring it.
    if (m_settings.contains("size")) {
        if (!m_settings.contains("scale")) m_settings.setValue("scale",1.0);
        m_settings.remove("size");syncSettings();
    }
}
Controller::~Controller() {
    m_destroying = true;
    m_windowTracker.configure(false,false);
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); }
    if (!m_stage.isEmpty()) QDir(m_stage).removeRecursively();
    // Terminate together, preserving enabled preferences for the next launch.
    for (auto& [id, pet] : m_pets) {pet->killTimer.stop();if (isRunning(id)) pet->process.terminate();}
    for (auto& [id, pet] : m_pets) if (isRunning(id) && !pet->process.waitForFinished(1500)) {
        pet->process.kill();pet->process.waitForFinished(500);
    }
    if (m_ruleApplied) setLayerRule(false);
}
void Controller::fail(const QString& text) { m_error = text; emit errorChanged(); }
void Controller::clearError() { m_error.clear(); emit errorChanged(); }
QVariantMap Controller::selected() const {
    const auto* r = m_catalog.find(m_selectedId);
    if (!r) return {};
    return {{"id",r->id},{"name",r->name},{"english",r->english},{"skin",r->skin},{"kind",r->kind},
            {"rarity",r->rarity},{"available",r->available},{"downloaded",r->downloaded},
            {"enabled",isEnabled(r->id)},{"running",isRunning(r->id)},
            {"autoActivity",effectivePreferences(r->id).value("autoActivity")}};
}
QVariantMap Controller::preferences() const {
    // INI values may reload as strings. Export stable types to QML so the string
    // "false" never becomes a truthy switch value after restarting the app.
    const double scale=m_settings.value("scale",1.0).toDouble();
    return {{"scale",std::isfinite(scale)?std::clamp(scale,.25,2.5):1.0},{"fps",m_settings.value("fps",30).toInt()},
            {"monitor",m_settings.value("monitor",QString()).toString()},{"layer",m_settings.value("layer","top").toString()},
            {"clickThrough",m_settings.value("clickThrough",false).toBool()},{"flip",m_settings.value("flip",false).toBool()},
            {"autoActivity",m_settings.value("autoActivity",false).toBool()},
            {"gravity",m_settings.value("gravity",false).toBool()},
            {"windowEdges",m_settings.value("windowEdges",false).toBool()},
            {"trackMovingWindows",m_settings.value("trackMovingWindows",false).toBool()},
            {"right",m_settings.value("right",64).toInt()},{"bottom",m_settings.value("bottom",40).toInt()}};
}
void Controller::setFilter(const QString& q, const QString& kind, bool downloadedOnly) {
    m_filter.setQuery(q, kind, downloadedOnly); emit catalogChanged();
}
void Controller::selectModel(const QString& id) {
    if (!m_catalog.find(id)) return;
    m_selectedId = id; m_settings.setValue("lastModel",id); loadAnimations(); emit selectionChanged();
}
void Controller::fetch(const QUrl& url, qint64 maximum, std::function<void(QByteArray,QString)> done) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent","ArkPets-Linux/0.7.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = m_network.get(request); m_reply = reply;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply,&QIODevice::readyRead,this,[reply,bytes,maximum] {
        if (bytes->size() + reply->bytesAvailable() > maximum) {
            reply->setProperty("sizeExceeded",true); reply->abort(); return;
        }
        bytes->append(reply->readAll());
    });
    connect(reply,&QNetworkReply::downloadProgress,this,[this](qint64 received,qint64 total) {
        if (!m_downloadId.isEmpty()) {
            m_progress = (m_fileIndex + (total>0 ? double(received)/total : 0))/3.0;
            emit activityChanged();
        }
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,maximum,done=std::move(done)] {
        QString error;
        if (reply->property("sizeExceeded").toBool()) error = "下载文件超过大小限制。";
        else if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        else {
            bytes->append(reply->readAll());
            if (bytes->isEmpty() || bytes->size()>maximum) error = "下载文件为空或过大。";
        }
        if (m_reply == reply) m_reply = nullptr;
        reply->deleteLater();
        if (!m_destroying) done(*bytes,error);
    });
}
void Controller::refreshCatalog() {
    if (m_busy) return;
    clearError(); m_busy = true; m_status = "正在更新模型列表…"; m_progress = 0; emit activityChanged();
    QUrl url(m_base); QString prefix=m_base.path(); if (!prefix.endsWith('/')) prefix+='/';
    url.setPath(prefix+"models_data.json");
    fetch(url,8*1024*1024,[this](QByteArray bytes,QString error) {
        if (error.isEmpty() && !m_catalog.load(bytes,&error)) { /* Keep the old catalog on parse errors. */ }
        if (error.isEmpty() && !saveBytes(m_root+"/catalog.json",bytes)) error = "列表已更新，但无法保存离线缓存。";
        m_busy=false;
        m_status = error.isEmpty() ? QString("模型列表已更新 · %1 个模型").arg(totalCount()) : "连接失败，可继续使用已缓存的列表和模型";
        emit activityChanged(); emit catalogChanged();
        if (!error.isEmpty()) fail(error);
        if (m_catalog.find(m_selectedId)) { loadAnimations(); emit selectionChanged(); }
        emit catalogReady();
    });
}
void Controller::downloadSelected() {
    const auto* record=m_catalog.find(m_selectedId);
    if (m_busy || !record) return;
    if (!record->available) { fail("这个模型在索引中缺少完整资源，暂时无法下载。"); return; }
    if (record->downloaded) return;
    clearError(); m_download=*record; m_downloadId=record->id;
    m_downloadFiles.clear();
    for (const QString& extension : {QString(".atlas"),QString(".skel"),QString(".png")})
        m_downloadFiles.append(record->files.value(extension).toString());
    m_stage=m_root+"/downloads/download-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(m_stage)) { m_stage.clear();m_downloadId.clear();fail("无法创建下载目录。");return; }
    m_busy=true; m_cancelled=false; m_progress=0; m_fileIndex=0; m_checksums={};
    m_status="正在下载 "+record->name+" · "+record->skin; emit activityChanged(); nextFile();
}
void Controller::nextFile() {
    if (m_fileIndex==m_downloadFiles.size()) { finishDownload(); return; }
    const QString file=m_downloadFiles[m_fileIndex];
    fetch(CatalogModel::assetUrl(m_base,m_download,file),file.endsWith(".png") ? 64*1024*1024 : 16*1024*1024,
          [this,file](QByteArray bytes,QString error) {
        if (m_cancelled) { finishDownload("已取消下载"); return; }
        if (error.isEmpty() && !saveBytes(m_stage+'/'+file,bytes)) error="无法写入模型文件，请检查磁盘空间。";
        if (!error.isEmpty()) { finishDownload(error);return; }
        m_checksums.insert(file,QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));
        ++m_fileIndex; m_progress=m_fileIndex/3.0; emit activityChanged(); nextFile();
    });
}
bool Controller::verifyDownload(QString* error) const {
    QFile skeleton(m_stage+'/'+m_download.files.value(".skel").toString());
    if (!skeleton.open(QIODevice::ReadOnly)) { *error="无法读取下载的骨骼文件。";return false; }
    QByteArray header=skeleton.read(2048); qsizetype offset=0;
    auto string=[&]() -> QByteArray {
        quint32 length=0;
        for (int shift=0; shift<=28; shift+=7) {
            if (offset>=header.size()) return {};
            auto b=static_cast<unsigned char>(header[offset++]);length|=quint32(b&127)<<shift;
            if (!(b&128)) {
                if (!length) return {};
                if (length>1024 || offset+length-1>header.size()) return {};
                QByteArray value=header.mid(offset,length-1);offset+=length-1;return value;
            }
        }
        return {};
    };
    string();
    if (!string().startsWith("3.8.")) { *error="该模型不是受支持的 Spine 3.8 二进制格式。";return false; }
    QImageReader image(m_stage+'/'+m_download.files.value(".png").toString());
    QSize size=image.size();
    if (!size.isValid() || size.width()>16384 || size.height()>16384 || qint64(size.width())*size.height()>32*1024*1024 || image.read().isNull()) {
        *error="下载的贴图损坏或尺寸过大。";return false;
    }
    QFile atlas(m_stage+'/'+m_download.files.value(".atlas").toString());
    if (!atlas.open(QIODevice::ReadOnly)) { *error="无法读取图集。";return false; }
    bool pageExpected=true;int pages=0;
    for (const auto& line : QString::fromUtf8(atlas.readAll()).split('\n')) {
        QString name=line.trimmed();
        if (name.isEmpty()) { pageExpected=true;continue; }
        if (pageExpected) {
            if (name!=m_download.files.value(".png").toString()) {
                *error="此图集需要索引中未提供的贴图，暂时无法使用。";return false;
            }
            ++pages; pageExpected=false;
        }
    }
    if (!pages) { *error="图集为空。";return false; }
    QString skelName=m_download.files.value(".skel").toString();skelName.chop(5);
    if (skelName+".atlas" != m_download.files.value(".atlas").toString()) {
        *error="骨骼与图集的文件名不匹配。";return false;
    }
    return true;
}
void Controller::finishDownload(const QString& initialError) {
    QString error=initialError, id=m_downloadId;
    if (error.isEmpty()) verifyDownload(&error);
    if (error.isEmpty()) {
        QJsonObject metadata{{"id",id},{"model",m_download.original},{"sha256",m_checksums},{"source",m_base.toString()}};
        if (!saveBytes(m_stage+"/complete.json",QJsonDocument(metadata).toJson())) error="无法保存下载记录。";
    }
    if (error.isEmpty()) {
        QString target=m_catalog.modelPath(id);
        // Keep a failed/partial earlier install until its replacement is ready.
        QString previous=m_stage+"-previous";
        bool movedPrevious=false;
        if (QFileInfo::exists(target)) {
            movedPrevious=QDir().rename(target,previous);
            if (!movedPrevious) error="无法替换不完整的本地模型，请检查目录权限。";
        }
        if (error.isEmpty() && !QDir().rename(m_stage,target)) {
            error="无法完成模型安装，请检查磁盘权限。";
            if (movedPrevious) QDir().rename(previous,target);
        } else if (error.isEmpty() && movedPrevious) {
            QDir(previous).removeRecursively();
        }
    }
    if (!m_stage.isEmpty() && QFileInfo::exists(m_stage)) QDir(m_stage).removeRecursively();
    m_stage.clear();m_busy=false;m_downloadId.clear();m_progress=error.isEmpty()?1:0;
    m_status=error.isEmpty()?m_download.name+" 已下载，可以显示到桌面":(m_cancelled?"下载已取消":"下载失败，可以重试");
    m_catalog.refreshDownloaded();
    m_animationCache.remove(id);
    if (m_selectedId==id) loadAnimations();
    emit activityChanged();emit selectionChanged();
    if (!error.isEmpty() && !m_cancelled) fail(error);
    if (error.isEmpty()) emit downloadFinished(id);
}
void Controller::cancelDownload() {
    if (m_downloadId.isEmpty()) return;
    m_cancelled=true;
    if (m_reply) m_reply->abort();
}
void Controller::removeSelected() {
    const auto* r=m_catalog.find(m_selectedId);
    if (!r || m_busy) return;
    if (isRunning(r->id)) { fail("请先停止这个桌宠，再删除模型。");return; }
    if (!QDir(m_catalog.modelPath(r->id)).removeRecursively()) { fail("删除失败，请检查文件权限。");return; }
    m_settings.remove(settingsPrefix(r->id));m_settings.sync();m_animationCache.remove(r->id);
    m_catalog.refreshDownloaded();m_animations.clear();emit selectionChanged();
    m_status=r->name+" 的本地模型已删除";emit activityChanged();
}
QString Controller::rendererPath() const {
    const QString dir=QCoreApplication::applicationDirPath();
    for (const QString& candidate : {dir+"/arkpets-renderer",dir+"/../libexec/arkpets/arkpets-renderer"})
        if (QFileInfo(candidate).isExecutable()) return QFileInfo(candidate).absoluteFilePath();
    return dir+"/arkpets-renderer";
}
void Controller::loadAnimations() { m_animations=modelAnimations(m_selectedId); }
QStringList Controller::modelAnimations(const QString& id) {
    const auto* r=m_catalog.find(id);
    if (!r || !r->downloaded) return {};
    if (m_animationCache.contains(id)) return m_animationCache.value(id);
    QProcess reader;
    reader.start(rendererPath(),{"--model",m_catalog.modelPath(id),"--list-animations"});
    if (!reader.waitForFinished(2500)) { reader.kill();reader.waitForFinished(500);fail(r->name+"：读取模型动作超时。");return {}; }
    if (reader.exitCode()!=0) { fail(r->name+"：模型读取失败："+QString::fromUtf8(reader.readAllStandardError()));return {}; }
    QStringList animations;
    for (const auto& line : QString::fromUtf8(reader.readAllStandardOutput()).split('\n',Qt::SkipEmptyParts))
        animations.append(line.section('\t',0,0));
    if (!animations.isEmpty()) m_animationCache.insert(id,animations);
    return animations;
}
QString Controller::settingsPrefix(const QString& id) const { return "models/"+id+'/'; }
QString Controller::petNamespace(const QString& id) const {
    return "arkpets-hyprland-"+m_profileTag+'-'+QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex().left(12));
}
bool Controller::isEnabled(const QString& id) const { return m_settings.value(settingsPrefix(id)+"enabled",false).toBool(); }
bool Controller::isRunning(const QString& id) const {
    auto found=m_pets.find(id);
    return found!=m_pets.end() && found->second->process.state()!=QProcess::NotRunning;
}
bool Controller::petRunning() const {
    for (const auto& [id,pet] : m_pets) if (pet->process.state()!=QProcess::NotRunning) return true;
    return false;
}
QVariantList Controller::pets() const {
    QVariantList result;
    for (int row=0;row<m_catalog.rowCount();++row) {
        auto index=m_catalog.index(row);
        if (!index.data(CatalogModel::Downloaded).toBool()) continue;
        QString id=index.data(CatalogModel::Id).toString();
        result.append(QVariantMap{{"id",id},{"name",index.data(CatalogModel::Name)},
            {"skin",index.data(CatalogModel::Skin)},{"enabled",isEnabled(id)},{"running",isRunning(id)}});
    }
    return result;
}
QVariantMap Controller::defaultPosition(const QString& id) const {
    int index=0;
    for (const auto& value:pets()) {if (value.toMap()["id"].toString()==id) break;++index;}
    return {{"right",64+(index%4)*280},{"bottom",40+((index/4)%3)*120}};
}
QVariantMap Controller::effectivePreferences(const QString& id) const {
    auto result=preferences();
    const auto prefix=settingsPrefix(id);
    const double multiplier=m_settings.value(prefix+"multiplier",1.0).toDouble();
    result["scale"]=result["scale"].toDouble()*(std::isfinite(multiplier)?std::clamp(multiplier,.25,2.5):1.0);
    for (const auto* key:{"monitor","layer","autoActivity","clickThrough","flip"}) {
        if (!m_settings.contains(prefix+key)) continue;
        const auto value=m_settings.value(prefix+key);
        result[key]=(QString(key)=="monitor" || QString(key)=="layer")?QVariant(value.toString()):QVariant(value.toBool());
    }
    const auto position=defaultPosition(id);
    for (const auto* key:{"right","bottom"}) result[key]=m_settings.value(prefix+key,position[key]).toInt();
    return result;
}
QVariantMap Controller::modelSettings(const QString& id) const {
    const auto* record=m_catalog.find(id);
    if (!record || !record->downloaded) return {};
    const auto prefix=settingsPrefix(id);
    const auto effective=effectivePreferences(id);
    QVariantMap overrides;
    for (const auto* key:{"monitor","layer","autoActivity","clickThrough","flip"})
        if (m_settings.contains(prefix+key)) overrides[key]=effective[key];
    const auto found=m_pets.find(id);
    auto* pet=found==m_pets.end()?nullptr:found->second.get();
    return {{"id",id},{"name",record->name},{"skin",record->skin},{"enabled",isEnabled(id)},
        {"running",isRunning(id)},{"ready",pet && pet->ready},{"error",pet?pet->error:QString()},
        {"multiplier",m_settings.value(prefix+"multiplier",1.0).toDouble()},
        {"animation",m_settings.value(prefix+"animation").toString()},
        {"overrides",overrides},{"effective",effective},{"namespace",petNamespace(id)},
        {"pid",pet?pet->process.processId():0}};
}
bool Controller::syncSettings() {
    m_settings.sync();
    if (m_settings.status()==QSettings::NoError) return true;
    fail("无法保存设置，请检查配置目录权限。");return false;
}
void Controller::notifyPets() {
    const auto p=preferences(); bool needed=false;
    for (const auto& [id,pet]:m_pets) needed|=isRunning(id) && pet->windowEdges;
    m_windowTracker.configure(!m_destroying && needed && p["windowEdges"].toBool(),p["trackMovingWindows"].toBool());
    emit petsChanged();emit petChanged();emit selectionChanged();
}
void Controller::sendWindowEdges() {
    for (const auto& [id,pet]:m_pets)
        if (isRunning(id) && !pet->stopping && pet->windowEdges && pet->process.bytesToWrite()<65536)
            pet->process.write(m_windowTracker.frame(pet->monitor));
}
Controller::PetSession& Controller::session(const QString& id) {
    auto& holder=m_pets[id];
    if (holder) return *holder;
    holder=std::make_unique<PetSession>();
    auto& pet=*holder;
    pet.killTimer.setSingleShot(true);
    connect(&pet.killTimer,&QTimer::timeout,&pet.process,&QProcess::kill);
    connect(&pet.process,&QProcess::readyReadStandardOutput,this,[this,id]{readPetOutput(id);});
    connect(&pet.process,&QProcess::readyReadStandardError,this,[this,id]{readPetError(id);});
    connect(&pet.process,&QProcess::stateChanged,this,[this]{notifyPets();});
    connect(&pet.process,&QProcess::started,this,&Controller::sendWindowEdges);
    connect(&pet.process,&QProcess::errorOccurred,this,[this,id](QProcess::ProcessError error) {
        if (error!=QProcess::FailedToStart || m_destroying) return;
        auto& pet=*m_pets.at(id);pet.ready=false;pet.stopping=false;pet.killTimer.stop();
        pet.error="桌宠启动失败："+pet.process.errorString();
        m_settings.setValue(settingsPrefix(id)+"enabled",false);syncSettings();
        const auto* record=m_catalog.find(id);
        fail((record?record->name:id)+"："+pet.error);notifyPets();releaseLayerRule();
    });
    connect(&pet.process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
            [this,id](int code,QProcess::ExitStatus status) {
        auto& pet=*m_pets.at(id);
        readPetOutput(id);readPetError(id);pet.killTimer.stop();pet.ready=false;pet.resettingPosition=false;
        const bool expected=pet.stopping;pet.stopping=false;
        if (m_destroying) return;
        if (expected && isEnabled(id)) {
            QTimer::singleShot(0,this,[this,id]{if (isEnabled(id) && !isRunning(id)) launch(id);});
        } else if (!expected) {
            m_settings.setValue(settingsPrefix(id)+"enabled",false);syncSettings();
            if (code!=0 || status==QProcess::CrashExit) {
                QString diagnostic=QString::fromUtf8(pet.diagnostics).trimmed();
                qsizetype fatal=diagnostic.lastIndexOf("错误:");if (fatal>=0) diagnostic=diagnostic.mid(fatal);
                pet.error=(status==QProcess::CrashExit?QString("桌宠渲染进程异常退出（代码 %1）"):QString("桌宠渲染失败（退出码 %1）")).arg(code);
                if (!diagnostic.isEmpty()) pet.error+="：\n"+diagnostic.right(1500);
                const auto* record=m_catalog.find(id);fail((record?record->name:id)+"："+pet.error);
            }
        }
        notifyPets();releaseLayerRule();
    });
    return pet;
}
bool Controller::setLayerRule(bool enabled) {
    if (!enabled && !m_ruleApplied) return true;
    QProcess process;
    const QString key="arkpets_linux_rule_"+m_profileTag;
    const QString code=enabled ? QString(
        "if _G.%1 then _G.%1:set_enabled(true) else _G.%1=hl.layer_rule({name=\"arkpets-%2\",match={namespace=\"^arkpets-hyprland-%2-.*$\"},blur=false,blur_popups=false,dim_around=false,no_anim=true}) end").arg(key,m_profileTag) :
        QString("if _G.%1 then _G.%1:set_enabled(false) end").arg(key);
    process.start("hyprctl",{"eval",code});
    bool done=process.waitForFinished(2000);
    if (!done) {process.kill();process.waitForFinished(500);}
    bool ok=done && process.exitCode()==0 && process.readAllStandardOutput().trimmed()=="ok";
    if (ok) m_ruleApplied=enabled;
    else if (enabled) fail("无法设置 Hyprland 桌宠规则。请确认处于 Hyprland 0.55+ 会话中。");
    return ok;
}
void Controller::releaseLayerRule() {
    if (petRunning()) return;
    for (const auto& value:pets()) if (value.toMap()["enabled"].toBool()) return;
    if (m_ruleApplied) setLayerRule(false);
}
void Controller::launch(const QString& id) {
    if (m_destroying || !isEnabled(id) || isRunning(id)) return;
    const auto* record=m_catalog.find(id);
    auto animations=modelAnimations(id);
    if (!record || !record->downloaded || animations.isEmpty() || !setLayerRule(true)) {
        m_settings.setValue(settingsPrefix(id)+"enabled",false);syncSettings();notifyPets();releaseLayerRule();return;
    }
    QString animation=m_settings.value(settingsPrefix(id)+"animation").toString();
    if (!animations.contains(animation)) {
        auto relax=std::find_if(animations.cbegin(),animations.cend(),[](const auto& name){return name.compare("Relax",Qt::CaseInsensitive)==0;});
        animation=relax!=animations.cend()?*relax:animations.first();
        m_settings.setValue(settingsPrefix(id)+"animation",animation);
    }
    const auto p=effectivePreferences(id);
    for (const auto* key:{"right","bottom"}) m_settings.setValue(settingsPrefix(id)+key,p[key]);
    if (!syncSettings()) return;
    QStringList args{"--model",m_catalog.modelPath(id),"--animation",animation,"--namespace",petNamespace(id),
        "--scale",QString::number(p["scale"].toDouble(),'g',15),"--fps",p["fps"].toString(),"--layer",p["layer"].toString(),
        "--right",p["right"].toString(),"--bottom",p["bottom"].toString()};
    QString monitor=p["monitor"].toString();
    if (monitor.isEmpty() || !m_monitors.contains(monitor)) {
        if (auto* screen=QGuiApplication::primaryScreen()) monitor=screen->name();
    }
    if (!monitor.isEmpty()) args<<"--monitor"<<monitor;
    if (p["clickThrough"].toBool()) args<<"--click-through";
    if (p["flip"].toBool()) args<<"--flip";
    if (p["autoActivity"].toBool()) args<<"--auto-activity";
    if (p["gravity"].toBool()) args<<"--gravity";
    if (p["windowEdges"].toBool()) args<<"--window-edges";
    auto& pet=session(id);pet.output.clear();pet.diagnostics.clear();pet.error.clear();pet.ready=false;pet.stopping=false;
    pet.monitor=monitor;pet.windowEdges=p["windowEdges"].toBool();
    pet.process.start(rendererPath(),args);notifyPets();
}
void Controller::stopSession(const QString& id) {
    if (!isRunning(id)) return;
    auto& pet=*m_pets.at(id);
    if (pet.stopping) return;
    pet.stopping=true;pet.process.terminate();pet.killTimer.start(2000);
}
void Controller::restartPet(const QString& id) {
    if (!isEnabled(id)) return;
    if (isRunning(id)) stopSession(id);else launch(id);
}
void Controller::startPet(const QString& requested) {
    const auto* record=m_catalog.find(m_selectedId);
    auto animations=modelAnimations(m_selectedId);
    if (!record || !record->downloaded || animations.isEmpty()) {fail("请先下载一个可播放的模型。");return;}
    if (!requested.isEmpty() && !animations.contains(requested)) {fail("该模型没有所选动作。");return;}
    if (!requested.isEmpty()) m_settings.setValue(settingsPrefix(record->id)+"animation",requested);
    m_settings.setValue(settingsPrefix(record->id)+"enabled",true);
    if (!syncSettings()) return;
    clearError();restartPet(record->id);notifyPets();
}
void Controller::setPetEnabled(const QString& id,bool enabled) {
    const auto* record=m_catalog.find(id);
    if (!record || !record->downloaded) {fail("请先下载这个模型。");return;}
    m_settings.setValue(settingsPrefix(id)+"enabled",enabled);
    if (!syncSettings()) return;
    clearError();
    if (enabled) {if (!isRunning(id)) launch(id);}
    else stopSession(id);
    notifyPets();releaseLayerRule();
}
void Controller::stopPet() {
    for (const auto& value:pets()) m_settings.setValue(settingsPrefix(value.toMap()["id"].toString())+"enabled",false);
    for (const auto& [id,pet]:m_pets) m_settings.setValue(settingsPrefix(id)+"enabled",false);
    syncSettings();for (const auto& [id,pet]:m_pets) stopSession(id);
    notifyPets();releaseLayerRule();
}
void Controller::restorePets() {
    for (const auto& value:pets()) {
        QString id=value.toMap()["id"].toString();if (isEnabled(id)) launch(id);
    }
}
void Controller::savePreferences(const QVariantMap& values) {
    auto next=preferences();for (auto i=values.cbegin();i!=values.cend();++i) next[i.key()]=i.value();
    if (values.contains("gravity") && !values["gravity"].toBool() && !values.value("windowEdges",false).toBool()) next["windowEdges"]=false;
    if (next["windowEdges"].toBool()) next["gravity"]=true;
    const double scale=next["scale"].toDouble();
    const int fps=next["fps"].toInt();
    if (!std::isfinite(scale) || scale<.25 || scale>2.5 || fps<1 || fps>120 || !QStringList{"top","bottom","overlay"}.contains(next["layer"].toString())) {
        fail("设置值超出允许范围。");return;
    }
    QMap<QString,QVariantMap> before;
    for (const auto& value:pets()) {QString id=value.toMap()["id"].toString();before[id]=effectivePreferences(id);}
    for (const auto* key:{"scale","fps","monitor","layer","clickThrough","flip","autoActivity","gravity","windowEdges","trackMovingWindows"}) m_settings.setValue(key,next[key]);
    if (!syncSettings()) return;
    clearError();emit preferencesChanged();notifyPets();m_status="通用设置已保存";emit activityChanged();
    for (auto i=before.cbegin();i!=before.cend();++i) if (i.value()!=effectivePreferences(i.key())) restartPet(i.key());
}
void Controller::saveModelSettings(const QString& id,const QVariantMap& values) {
    const auto* record=m_catalog.find(id);
    if (!record || !record->downloaded) {fail("请先下载这个模型。");return;}
    const double multiplier=values.value("multiplier",1.0).toDouble();
    const auto overrides=values.value("overrides").toMap();
    if (!std::isfinite(multiplier) || multiplier<.25 || multiplier>2.5 ||
        (overrides.contains("layer") && !QStringList{"top","bottom","overlay"}.contains(overrides["layer"].toString()))) {
        fail("设置值超出允许范围。");return;
    }
    QString animation=values.value("animation",m_settings.value(settingsPrefix(id)+"animation")).toString();
    if (values.contains("animation") && !animation.isEmpty() && !modelAnimations(id).contains(animation)) {
        fail("该模型没有所选动作。");return;
    }
    const auto before=effectivePreferences(id);
    const QString oldAnimation=m_settings.value(settingsPrefix(id)+"animation").toString();
    m_settings.setValue(settingsPrefix(id)+"multiplier",multiplier);
    m_settings.setValue(settingsPrefix(id)+"animation",animation);
    for (const auto* key:{"monitor","layer","autoActivity","clickThrough","flip"}) {
        if (overrides.contains(key)) m_settings.setValue(settingsPrefix(id)+key,overrides[key]);
        else m_settings.remove(settingsPrefix(id)+key);
    }
    if (!syncSettings()) return;
    clearError();notifyPets();m_status=record->name+" 的设置已保存";emit activityChanged();
    if (before!=effectivePreferences(id) || oldAnimation!=animation) restartPet(id);
}
void Controller::resetPosition(const QString& id) {
    const auto targets=id.isEmpty()?pets():QVariantList{QVariantMap{{"id",id}}};
    for (const auto& value:targets) {
        QString modelId=value.toMap()["id"].toString();
        const auto* record=m_catalog.find(modelId);if (!record || !record->downloaded) continue;
        if (isRunning(modelId)) m_pets.at(modelId)->resettingPosition=true;
        const auto position=defaultPosition(modelId);
        for (const auto* key:{"right","bottom"}) m_settings.setValue(settingsPrefix(modelId)+key,position[key]);
    }
    if (!syncSettings()) return;
    notifyPets();for (const auto& value:targets) restartPet(value.toMap()["id"].toString());
}
void Controller::refreshMonitors() {
    m_monitors.clear();for (auto* screen:QGuiApplication::screens()) m_monitors.append(screen->name());
    emit monitorsChanged();
}
void Controller::readPetOutput(const QString& id) {
    auto& pet=*m_pets.at(id);
    pet.output+=pet.process.readAllStandardOutput();
    qsizetype end;
    while ((end=pet.output.indexOf('\n'))>=0) {
        QByteArray line=pet.output.left(end);pet.output.remove(0,end+1);
        if (line.startsWith("ARKPETS_POSITION ") && !pet.resettingPosition) {
            auto values=line.mid(17).split(' ');
            if (values.size()==2) {
                bool a,b;int right=values[0].toInt(&a),bottom=values[1].toInt(&b);
                if (a && b && right>=-20000 && bottom>=-20000 && right<=20000 && bottom<=20000) {
                    m_settings.setValue(settingsPrefix(id)+"right",right);m_settings.setValue(settingsPrefix(id)+"bottom",bottom);
                    syncSettings();
                }
            }
        } else if (line=="ARKPETS_READY") {
            pet.ready=true;const auto* record=m_catalog.find(id);
            m_status=(record?record->name:id)+" 已显示到桌面";emit activityChanged();notifyPets();
        }
    }
}
void Controller::readPetError(const QString& id) {
    auto& pet=*m_pets.at(id);pet.diagnostics+=pet.process.readAllStandardError();
    if (pet.diagnostics.size()>16384) pet.diagnostics=pet.diagnostics.right(16384);
}
void Controller::openDataDirectory() {QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));}
