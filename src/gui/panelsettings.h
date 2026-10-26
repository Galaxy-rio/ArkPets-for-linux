#pragma once
#include <QObject>
#include <QSettings>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QVariantMap>
#include <optional>

class PanelSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString palette READ palette WRITE setPalette NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString seed READ seed NOTIFY changed)
    Q_PROPERTY(bool monochrome READ monochrome NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(QVariantMap lightScheme READ lightScheme NOTIFY changed)
    Q_PROPERTY(QVariantMap darkScheme READ darkScheme NOTIFY changed)
    Q_PROPERTY(QString systemStatus READ systemStatus NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool sidebarQuickAdjust READ sidebarQuickAdjust WRITE setSidebarQuickAdjust NOTIFY sidebarChanged)
    Q_PROPERTY(bool sidebarEnabledOnly READ sidebarEnabledOnly WRITE setSidebarEnabledOnly NOTIFY sidebarChanged)
public:
    explicit PanelSettings(QString configRoot = {}, QStringList palettePaths = {},
                           std::optional<QString> executable = std::nullopt, QObject* parent = nullptr);
    QString mode() const {return m_mode;}
    QString palette() const {return m_palette;}
    bool dark() const;
    QString seed() const;
    bool monochrome() const {return m_palette == "monochrome";}
    QVariantList presets() const;
    QVariantMap lightScheme() const {return m_palette == "matugen" ? m_light : QVariantMap{};}
    QVariantMap darkScheme() const {return m_palette == "matugen" ? m_dark : QVariantMap{};}
    QString systemStatus() const {return m_status;}
    QString error() const {return m_error;}
    bool sidebarQuickAdjust() const {return m_sidebarQuickAdjust;}
    bool sidebarEnabledOnly() const {return m_sidebarEnabledOnly;}
    void setMode(const QString& value);
    void setPalette(const QString& value);
    void setSidebarQuickAdjust(bool value);
    void setSidebarEnabledOnly(bool value);
    Q_INVOKABLE void refreshSystemPalette();
signals:
    void changed();
    void sidebarChanged();
private:
    void save(bool appearance = true);
    void watchPalette();
    QSettings m_settings;
    QString m_mode, m_palette, m_status, m_error;
    QStringList m_paths;
    std::optional<QString> m_executable;
    QVariantMap m_light, m_dark;
    bool m_systemDark = false;
    bool m_sidebarQuickAdjust = false, m_sidebarEnabledOnly = false;
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
};
