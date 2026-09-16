#pragma once

#include <QMap>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// The preset corpus ships grouped into categories (Dancer, Fractal, Waveform and so on), which
// is the only grouping anyone has actually curated. Using it beats inventing our own taxonomy.
class PresetLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY libraryChanged)
    Q_PROPERTY(QString curatedList READ curatedList WRITE setCuratedList NOTIFY libraryChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY libraryChanged)
    Q_PROPERTY(int count READ count NOTIFY libraryChanged)

public:
    explicit PresetLibrary(QObject *parent = nullptr);

    QString rootPath() const { return m_rootPath; }
    void setRootPath(const QString &path);
    QString curatedList() const { return m_curatedList; }
    void setCuratedList(const QString &path);

    QStringList categories() const { return m_categories; }
    int count() const;

    // [{ name, path }] for one category, for building a menu.
    Q_INVOKABLE QVariantList presets(const QString &category) const;
    // Just the paths, in order; an empty category means the whole library.
    Q_INVOKABLE QStringList paths(const QString &category = QString()) const;

signals:
    void libraryChanged();

private:
    void rescan();

    QString m_rootPath;
    QString m_curatedList;
    QStringList m_categories;
    QMap<QString, QList<QPair<QString, QString>>> m_byCategory;  // category -> [(name, path)]
};
