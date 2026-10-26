#include "panelsettings.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleHints>

namespace {
QString settingsPath(QString root) {
    if (root.isEmpty()) root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/arkpets";
    QDir().mkpath(root);
    return root + "/panel.ini";
}
const QStringList roles = {"primary", "on_primary", "primary_container", "on_primary_container",
    "secondary", "on_secondary", "secondary_container", "on_secondary_container",
    "tertiary", "on_tertiary", "tertiary_container", "on_tertiary_container",
    "error", "on_error", "error_container", "on_error_container", "background", "on_background",
    "surface", "on_surface", "surface_variant", "on_surface_variant", "outline", "outline_variant",
    "shadow", "scrim", "inverse_surface", "inverse_on_surface", "inverse_primary", "surface_dim",
    "surface_bright", "surface_container_lowest", "surface_container_low", "surface_container",
    "surface_container_high", "surface_container_highest"};
QVariantMap readScheme(const QJsonObject& colors, const QString& mode) {
    QVariantMap result;
    static const QRegularExpression hex("^#[0-9a-fA-F]{6}$");
    for (const QString& role : roles) {
        // Support both Matugen's per-role JSON and DMS's per-mode export.
        QJsonValue value = colors.value(mode).toObject().value(role);
        if (value.isUndefined()) value = colors.value(role).toObject().value(mode);
        if (value.isObject()) value = value.toObject()["color"];
        const QString color = value.toString();
        if (!hex.match(color).hasMatch()) return {};
        const QStringList words = role.split('_');
        QString key = words.first();
        for (int n = 1; n < words.size(); ++n) {
            QString word = words[n]; word[0] = word[0].toUpper(); key += word;
        }
        if (role.startsWith("on_")) key += "Color";
        result.insert(key, color);
    }
    return result;
}
}

PanelSettings::PanelSettings(QString root, QStringList paths, std::optional<QString> executable, QObject* parent)
    : QObject(parent), m_settings(settingsPath(root), QSettings::IniFormat),
      m_paths(std::move(paths)), m_executable(std::move(executable)) {
    m_mode = m_settings.value("mode", "system").toString();
    if (!QStringList{"light", "dark", "system"}.contains(m_mode)) m_mode = "system";
    m_palette = m_settings.value("palette", "green").toString();
    bool known = m_palette == "matugen";
    for (const auto& preset : presets()) known |= preset.toMap()["id"].toString() == m_palette;
    if (!known) m_palette = "green";
    m_sidebarQuickAdjust = m_settings.value("sidebarQuickAdjust", false).toBool();
    m_sidebarEnabledOnly = m_settings.value("sidebarEnabledOnly", false).toBool();
    if (m_paths.isEmpty()) {
        const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        m_paths = {config + "/matugen/colors.json", cache + "/matugen/colors.json", cache + "/dms/dms-colors.json"};
    }
    m_reload.setSingleShot(true); m_reload.setInterval(150);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {m_reload.start();});
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {m_reload.start();});
    connect(&m_reload, &QTimer::timeout, this, &PanelSettings::refreshSystemPalette);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {emit changed();});
    refreshSystemPalette();
}

QVariantList PanelSettings::presets() const {
    return {QVariantMap{{"id","blue"},{"label","蓝色"},{"seed","#2196f3"}},
        QVariantMap{{"id","purple"},{"label","紫色"},{"seed","#8e5ec0"}},
        QVariantMap{{"id","green"},{"label","绿色"},{"seed","#496b45"}},
        QVariantMap{{"id","orange"},{"label","橙色"},{"seed","#ef6c00"}},
        QVariantMap{{"id","red"},{"label","红色"},{"seed","#d1495b"}},
        QVariantMap{{"id","cyan"},{"label","青色"},{"seed","#008fa1"}},
        QVariantMap{{"id","pink"},{"label","粉色"},{"seed","#d81b60"}},
        QVariantMap{{"id","amber"},{"label","琥珀"},{"seed","#f9a825"}},
        QVariantMap{{"id","coral"},{"label","珊瑚"},{"seed","#ff8a80"}},
        QVariantMap{{"id","monochrome"},{"label","单色"},{"seed","#808080"}}};
}
QString PanelSettings::seed() const {
    for (const auto& preset : presets()) {
        const auto item = preset.toMap();
        if (item["id"].toString() == m_palette) return item["seed"].toString();
    }
    return "#496b45";
}
bool PanelSettings::dark() const {
    if (m_mode != "system") return m_mode == "dark";
    const auto system = QGuiApplication::styleHints()->colorScheme();
    return system == Qt::ColorScheme::Unknown ? m_systemDark : system == Qt::ColorScheme::Dark;
}
void PanelSettings::save(bool appearance) {
    const auto oldError = m_error;
    m_settings.setValue("mode", m_mode); m_settings.setValue("palette", m_palette);
    m_settings.setValue("sidebarQuickAdjust", m_sidebarQuickAdjust);
    m_settings.setValue("sidebarEnabledOnly", m_sidebarEnabledOnly);
    m_settings.sync();
    m_error = m_settings.status() == QSettings::NoError ? QString() : QStringLiteral("无法保存面板设置，请检查配置目录权限");
    if (appearance || oldError != m_error) emit changed();
}
void PanelSettings::setSidebarQuickAdjust(bool value) {
    if (value == m_sidebarQuickAdjust) return;
    m_sidebarQuickAdjust = value; save(false); emit sidebarChanged();
}
void PanelSettings::setSidebarEnabledOnly(bool value) {
    if (value == m_sidebarEnabledOnly) return;
    m_sidebarEnabledOnly = value; save(false); emit sidebarChanged();
}
void PanelSettings::setMode(const QString& value) {
    if (m_mode == value || !QStringList{"light", "dark", "system"}.contains(value)) return;
    m_mode = value; save();
}
void PanelSettings::setPalette(const QString& value) {
    if (m_palette == value) return;
    bool known = value == "matugen";
    for (const auto& preset : presets()) known |= preset.toMap()["id"].toString() == value;
    if (!known) return;
    m_palette = value; if (value == "matugen") refreshSystemPalette(); save();
}
void PanelSettings::watchPalette() {
    QStringList desired;
    for (const auto& path : m_paths) {
        if (QFileInfo::exists(path)) desired.append(path);
        QDir parent = QFileInfo(path).absoluteDir();
        while (!parent.exists() && parent.cdUp()) {}
        if (parent.exists()) desired.append(parent.absolutePath());
    }
    desired.removeDuplicates();
    const QStringList current = m_watcher.files() + m_watcher.directories();
    for (const auto& path : current) if (!desired.contains(path)) m_watcher.removePath(path);
    for (const auto& path : desired) if (!current.contains(path)) m_watcher.addPath(path);
}
void PanelSettings::refreshSystemPalette() {
    m_light.clear(); m_dark.clear(); m_systemDark = false;
    const QString executable = m_executable.value_or(QStandardPaths::findExecutable("matugen"));
    const bool installed = !executable.isEmpty() && QFileInfo(executable).isExecutable();
    m_status = installed ? "未找到完整的系统配色，使用默认绿色" : "未安装 matugen，使用默认绿色";
    for (const auto& path : m_paths) {
        QFile file(path);
        if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly)) continue;
        const auto json = QJsonDocument::fromJson(file.read(1024 * 1024 + 1)).object();
        const auto colors = json["colors"].toObject();
        const auto light = readScheme(colors, "light"), dark = readScheme(colors, "dark");
        if (light.isEmpty() || dark.isEmpty()) continue;
        m_systemDark = json["mode"].toString() == "dark";
        if (installed) {
            m_light = light; m_dark = dark;
            m_status = path.endsWith("/dms/dms-colors.json") ? "已读取系统配色 · DankMaterialShell" : "已读取系统配色 · matugen";
        }
        break;
    }
    watchPalette(); emit changed();
}
