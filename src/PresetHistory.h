#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

// Favourites and recently-seen visualisations.
//
// Recents matter more here than they would elsewhere, because the visualiser rotates on its own:
// without a history there is no way back to the one that just went past, which is precisely the
// moment someone decides they liked it. Every preset that becomes current is recorded, rotated
// ones included - that is the point, not an accident.
//
// Both lists address presets by file path rather than by name: two packs can carry presets of
// the same name, and a name cannot be reopened.
class PresetHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList favourites READ favourites NOTIFY favouritesChanged)
    Q_PROPERTY(QVariantList recents READ recents NOTIFY recentsChanged)
    Q_PROPERTY(int favouriteCount READ favouriteCount NOTIFY favouritesChanged)

public:
    explicit PresetHistory(QObject *parent = nullptr);

    // [{ name, title, path }], newest or user order first, with missing files filtered out.
    QVariantList favourites() const;
    QVariantList recents() const;
    int favouriteCount() const;

    Q_INVOKABLE bool isFavourite(const QString &path) const;
    Q_INVOKABLE void toggleFavourite(const QString &path);
    Q_INVOKABLE void clearRecents();
    // Called whenever a preset becomes current, including an automatic rotation.
    Q_INVOKABLE void noteUsed(const QString &path);

signals:
    void favouritesChanged();
    void recentsChanged();

private:
    QVariantList describe(const QStringList &paths) const;
    void save();

    QStringList m_favourites;
    QStringList m_recents;
};
