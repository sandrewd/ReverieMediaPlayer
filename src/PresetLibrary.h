#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// The preset corpus ships grouped into categories (Dancer, Fractal, Waveform and so on). Using
// the pack's own grouping beats inventing a taxonomy over nearly ten thousand presets.
//
// It ships a *second* level too - 184 sub-folders such as Glowsticks, Nested Spiral, Rorschach
// and Polar Warp - which this class discarded for a long time by keeping only the first path
// component. They describe what a preset looks like far better than the eleven categories above
// them, and better than the author name that was used as a substitute. See the brief.
//
// This is also the model the preset browser binds to. A cascading menu cannot present 9,795
// items - it was already a measurable stall at 480 - so the browser is a list with a search box,
// and the filtering happens here rather than by rebuilding a QVariantList in QML per keystroke.
class PresetLibrary : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY libraryChanged)
    Q_PROPERTY(QString curatedList READ curatedList WRITE setCuratedList NOTIFY libraryChanged)
    // The shipped list of visualisations measured to render nothing, and the switch that lets
    // someone look at them anyway. Hiding them is the default because a black stage reads as the
    // application having failed, not as the preset being at fault.
    Q_PROPERTY(QStringList blocklists READ blocklists WRITE setBlocklists NOTIFY libraryChanged)
    Q_PROPERTY(bool showBroken READ showBroken WRITE setShowBroken NOTIFY libraryChanged)
    Q_PROPERTY(int brokenCount READ brokenCount NOTIFY libraryChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY libraryChanged)
    Q_PROPERTY(int count READ count NOTIFY libraryChanged)

    // The browser's filters. Any of them may be empty, meaning "do not narrow by this".
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
    Q_PROPERTY(QString filterCategory READ filterCategory WRITE setFilterCategory NOTIFY filterChanged)
    Q_PROPERTY(QString filterStyle READ filterStyle WRITE setFilterStyle NOTIFY filterChanged)
    Q_PROPERTY(int filteredCount READ filteredCount NOTIFY filterChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,  // the full filename stem, "Author - Title"
        TitleRole,                    // just the title
        AuthorRole,
        CategoryRole,
        StyleRole,                    // the sub-folder, "" when the preset sits loose
        PathRole,
    };

    explicit PresetLibrary(QObject *parent = nullptr);

    QString rootPath() const { return m_rootPath; }
    void setRootPath(const QString &path);
    QString curatedList() const { return m_curatedList; }
    void setCuratedList(const QString &path);
    QStringList blocklists() const { return m_blocklists; }
    void setBlocklists(const QStringList &paths);
    bool showBroken() const { return m_showBroken; }
    void setShowBroken(bool show);
    // How many of the installed presets are on the blocklist, so the menu can say so rather than
    // offering a switch whose effect is invisible.
    int brokenCount() const { return m_brokenCount; }

    QStringList categories() const { return m_categories; }
    int count() const { return int(m_all.size()); }

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QString filterCategory() const { return m_filterCategory; }
    void setFilterCategory(const QString &category);
    QString filterStyle() const { return m_filterStyle; }
    void setFilterStyle(const QString &style);
    int filteredCount() const { return int(m_filtered.size()); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Clears every filter in one step, without three separate rebuilds.
    Q_INVOKABLE void clearFilters();
    // The sub-folders present in a category, sorted. Empty category means the whole library.
    Q_INVOKABLE QStringList styles(const QString &category = QString()) const;
    // Exactly what the browser is showing, in row order, so a selection can narrow the
    // visualiser's rotation to the same set the person is looking at.
    Q_INVOKABLE QStringList filteredPaths() const;

    // Just the paths, in order; an empty category means the whole library.
    Q_INVOKABLE QStringList paths(const QString &category = QString()) const;

    // artistGroups() and ungrouped() lived here to build the cascading picker's third level,
    // grouping by the author in the filename. They went with the cascade: the pack's own
    // sub-folders are a better grouping than the author, and the browser presents both as
    // columns rather than as nested menus.

    static QString artistOf(const QString &presetName);
    static QString titleOf(const QString &presetName);

signals:
    void libraryChanged();
    void filterChanged();

private:
    struct Entry {
        QString name;
        QString path;
        QString category;
        QString style;
    };

    void rescan();
    void rebuildFilter();

    // Relative paths, as they appear in the blocklist file. Comparing relative keeps one set
    // usable against the curated install and the full pack, which sit at different roots.
    QSet<QString> readBlocklist() const;

    QString m_rootPath;
    QString m_curatedList;
    QStringList m_blocklists;
    bool m_showBroken = false;
    int m_brokenCount = 0;
    QString m_searchText;
    QString m_filterCategory;
    QString m_filterStyle;
    QStringList m_categories;
    QList<Entry> m_all;
    QList<int> m_filtered;                                       // indices into m_all
    QMap<QString, QList<QPair<QString, QString>>> m_byCategory;  // category -> [(name, path)]
};
