#include "catalog.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

CatalogModel::CatalogModel(QString root, QObject* parent) : QAbstractListModel(parent), m_root(std::move(root)) {}
int CatalogModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_records.size(); }
QVariant CatalogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size()) return {};
    const auto& r = m_records[index.row()];
    switch (role) {
    case Id: return r.id;
    case Name: return r.name;
    case English: return r.english;
    case Skin: return r.skin;
    case Kind: return r.kind;
    case Rarity: return r.rarity;
    case Available: return r.available;
    case Downloaded: return r.downloaded;
    default: return {};
    }
}
QHash<int, QByteArray> CatalogModel::roleNames() const {
    return {{Id,"modelId"},{Name,"modelName"},{English,"englishName"},{Skin,"skinName"},
            {Kind,"modelKind"},{Rarity,"rarity"},{Available,"available"},{Downloaded,"downloaded"}};
}
bool CatalogModel::safeName(const QString& name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') &&
           !name.contains('\\') && !name.contains(QChar('\0')) && name.size() < 240;
}
QString CatalogModel::modelPath(const QString& id) const { return m_root + "/models/" + id; }
bool CatalogModel::load(const QByteArray& bytes, QString* error) {
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject() ||
        !doc.object().value("data").isObject() || !doc.object().value("storageDirectory").isObject()) {
        *error = "模型索引格式无效，已保留原有列表。"; return false;
    }
    QList<ModelRecord> records;
    const auto root = doc.object(), entries = root.value("data").toObject(), storage = root.value("storageDirectory").toObject();
    if (entries.isEmpty() || entries.size() > 50000) { *error = "模型索引数量异常。"; return false; }
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (!safeName(it.key()) || !it.value().isObject()) continue;
        auto object = it.value().toObject();
        ModelRecord r;
        r.id = it.key(); r.original = object;
        r.name = object.value("name").toString(r.id);
        r.english = object.value("appellation").toString();
        r.skin = object.value("skinGroupName").toString("默认外观");
        if (r.skin.isEmpty()) r.skin = "默认外观";
        r.kind = object.value("type").toString();
        r.folder = storage.value(r.kind).toString();
        r.files = object.value("assetList").toObject();
        const bool folderValid = (r.kind == "Operator" && r.folder == "models") ||
            (r.kind == "Enemy" && r.folder == "models_enemies") ||
            (r.kind == "DynIllust" && r.folder == "models_illust");
        r.available = folderValid && r.files.size() == 3;
        for (const QString& extension : {QString(".atlas"), QString(".png"), QString(".skel")}) {
            QString filename = r.files.value(extension).toString();
            r.available = r.available && safeName(filename) && filename.endsWith(extension);
        }
        for (const auto tag : object.value("sortTags").toArray()) {
            auto text = tag.toString();
            if (text.startsWith("Rarity_")) r.rarity = std::clamp(text.mid(7).toInt(), 0, 6);
        }
        records.append(r);
    }
    if (records.isEmpty()) { *error = "索引中没有可识别的模型。"; return false; }
    std::stable_sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        int name = QString::localeAwareCompare(a.name, b.name);
        return name != 0 ? name < 0 : a.id < b.id;
    });
    beginResetModel(); m_records = std::move(records); endResetModel();
    refreshDownloaded(); return true;
}
const ModelRecord* CatalogModel::find(const QString& id) const {
    for (const auto& r : m_records) if (r.id == id) return &r;
    return nullptr;
}
void CatalogModel::refreshDownloaded() {
    for (auto& r : m_records) {
        QString dir = modelPath(r.id);
        QFile metadata(dir + "/complete.json");
        r.downloaded = metadata.open(QIODevice::ReadOnly) &&
            QJsonDocument::fromJson(metadata.readAll()).object().value("id").toString() == r.id;
        for (auto it = r.files.begin(); it != r.files.end(); ++it)
            r.downloaded = r.downloaded && QFileInfo(dir + '/' + it.value().toString()).size() > 0;
    }
    if (!m_records.isEmpty()) emit dataChanged(index(0), index(m_records.size()-1), {Downloaded});
    emit countsChanged();
}
int CatalogModel::downloadedCount() const {
    return std::count_if(m_records.begin(), m_records.end(), [](const auto& r) { return r.downloaded; });
}
QUrl CatalogModel::assetUrl(const QUrl& base, const ModelRecord& model, const QString& file) {
    QUrl url(base);
    QString prefix = base.path(); if (!prefix.endsWith('/')) prefix += '/';
    url.setPath(prefix + model.folder + '/' + model.id + '/' + file, QUrl::DecodedMode);
    return url;
}
void CatalogFilter::setQuery(QString query, QString kind, bool only) {
    beginFilterChange(); m_query = query.simplified(); m_kind = kind; m_downloadedOnly = only;
    endFilterChange(Direction::Rows);
}
bool CatalogFilter::filterAcceptsRow(int row, const QModelIndex& parent) const {
    auto idx = sourceModel()->index(row, 0, parent);
    if (!m_kind.isEmpty() && idx.data(CatalogModel::Kind).toString() != m_kind) return false;
    if (m_downloadedOnly && !idx.data(CatalogModel::Downloaded).toBool()) return false;
    QString haystack;
    for (int role : {CatalogModel::Name, CatalogModel::English, CatalogModel::Skin, CatalogModel::Id})
        haystack += idx.data(role).toString() + ' ';
    for (const auto& term : m_query.split(' ', Qt::SkipEmptyParts))
        if (!haystack.contains(term, Qt::CaseInsensitive)) return false;
    return true;
}
