#pragma once
#include <QAbstractListModel>
#include <QJsonObject>
#include <QSortFilterProxyModel>
#include <QUrl>

struct ModelRecord {
    QString id, name, english, skin, kind, folder;
    QJsonObject files, original;
    int rarity = 0;
    bool available = false, downloaded = false;
};

class CatalogModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { Id = Qt::UserRole+1, Name, English, Skin, Kind, Rarity, Available, Downloaded };
    explicit CatalogModel(QString dataRoot, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex&, int) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool load(const QByteArray& bytes, QString* error);
    const ModelRecord* find(const QString& id) const;
    void refreshDownloaded();
    int downloadedCount() const;
    QString modelPath(const QString& id) const;
    static bool safeName(const QString& name);
    static QUrl assetUrl(const QUrl& base, const ModelRecord& model, const QString& file);
signals:
    void countsChanged();
private:
    QString m_root;
    QList<ModelRecord> m_records;
};

class CatalogFilter : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setQuery(QString query, QString kind, bool downloadedOnly);
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
private:
    QString m_query, m_kind = "Operator";
    bool m_downloadedOnly = false;
};
