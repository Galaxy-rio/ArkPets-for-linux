#pragma once
#include "catalog.h"
#include "panelsettings.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <functional>
#include <map>
#include <memory>

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* models READ models CONSTANT)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY catalogChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY catalogChanged)
    Q_PROPERTY(int downloadedCount READ downloadedCount NOTIFY catalogChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY activityChanged)
    Q_PROPERTY(double progress READ progress NOTIFY activityChanged)
    Q_PROPERTY(QString status READ status NOTIFY activityChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString downloadId READ downloadId NOTIFY activityChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)
    Q_PROPERTY(PanelSettings* panel READ panel CONSTANT)
    Q_PROPERTY(QStringList monitors READ monitors NOTIFY monitorsChanged)
    Q_PROPERTY(QStringList animations READ animations NOTIFY selectionChanged)
    Q_PROPERTY(bool petRunning READ petRunning NOTIFY petChanged)
    Q_PROPERTY(QVariantList pets READ pets NOTIFY petsChanged)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
public:
    explicit Controller(QString dataRoot = {}, QString configRoot = {}, QUrl baseUrl = {}, QObject* parent = nullptr);
    ~Controller() override;
    QAbstractItemModel* models() { return &m_filter; }
    int totalCount() const { return m_catalog.rowCount(); }
    int visibleCount() const { return m_filter.rowCount(); }
    int downloadedCount() const { return m_catalog.downloadedCount(); }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString downloadId() const { return m_downloadId; }
    QVariantMap selected() const;
    QVariantMap preferences() const;
    PanelSettings* panel() {return &m_panel;}
    QStringList monitors() const { return m_monitors; }
    QStringList animations() const { return m_animations; }
    bool petRunning() const;
    QVariantList pets() const;
    QString dataDirectory() const { return m_root; }
    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE void setFilter(const QString& query, const QString& kind, bool downloadedOnly);
    Q_INVOKABLE void selectModel(const QString& id);
    Q_INVOKABLE void downloadSelected();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void startPet(const QString& animation = {});
    Q_INVOKABLE void stopPet();
    Q_INVOKABLE void restorePets();
    Q_INVOKABLE QVariantMap modelSettings(const QString& id) const;
    Q_INVOKABLE QStringList modelAnimations(const QString& id);
    Q_INVOKABLE void saveModelSettings(const QString& id, const QVariantMap& values);
    Q_INVOKABLE void setPetEnabled(const QString& id, bool enabled);
    Q_INVOKABLE void savePreferences(const QVariantMap& values);
    Q_INVOKABLE void resetPosition(const QString& id = {});
    Q_INVOKABLE void refreshMonitors();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void openDataDirectory();
    QString rendererPath() const;
signals:
    void catalogChanged();
    void activityChanged();
    void errorChanged();
    void selectionChanged();
    void preferencesChanged();
    void monitorsChanged();
    void petChanged();
    void petsChanged();
    void downloadFinished(const QString& id);
    void catalogReady();
private:
    QString m_root;
    QSettings m_settings;
    PanelSettings m_panel;
    CatalogModel m_catalog;
    CatalogFilter m_filter;
    QNetworkAccessManager m_network;
    QNetworkReply* m_reply = nullptr;
    QUrl m_base;
    bool m_busy = false, m_cancelled = false, m_destroying = false, m_ruleApplied = false;
    double m_progress = 0;
    QString m_status, m_error, m_selectedId, m_downloadId;
    QString m_stage;
    ModelRecord m_download;
    QStringList m_downloadFiles, m_animations, m_monitors;
    int m_fileIndex = 0;
    QJsonObject m_checksums;
    struct PetSession {
        QProcess process;
        QTimer killTimer;
        bool ready = false, stopping = false, resettingPosition = false;
        QByteArray output, diagnostics;
        QString error;
    };
    std::map<QString, std::unique_ptr<PetSession>> m_pets;
    QMap<QString, QStringList> m_animationCache;
    QString m_profileTag;
    void fail(const QString& text);
    void fetch(const QUrl& url, qint64 maximum, std::function<void(QByteArray, QString)> callback);
    void nextFile();
    void finishDownload(const QString& error = {});
    bool verifyDownload(QString* error) const;
    void loadAnimations();
    bool setLayerRule(bool enabled);
    QString settingsPrefix(const QString& id) const;
    QString petNamespace(const QString& id) const;
    QVariantMap effectivePreferences(const QString& id) const;
    QVariantMap defaultPosition(const QString& id) const;
    bool isEnabled(const QString& id) const;
    bool isRunning(const QString& id) const;
    bool syncSettings();
    void notifyPets();
    PetSession& session(const QString& id);
    void launch(const QString& id);
    void restartPet(const QString& id);
    void stopSession(const QString& id);
    void releaseLayerRule();
    void readPetOutput(const QString& id);
    void readPetError(const QString& id);
};
