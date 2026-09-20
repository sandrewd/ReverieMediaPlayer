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
    // Two properties rather than one list, unlike ProjectMItem: the rotation only needs to know
    // whether a preset is excluded, but the browser shows the excluded ones on request and has to
    // say *why* - "nothing will fix this" and "drop a texture in and it works" are different
    // answers and deserve different marks.
    Q_PROPERTY(QString blocklist READ blocklist WRITE setBlocklist NOTIFY libraryChanged)
    Q_PROPERTY(QString textureBlocklist READ textureBlocklist WRITE setTextureBlocklist NOTIFY libraryChanged)
    // Presets that use an external image but still draw without it. Informational only, and a
    // separate shipped file because working it out means reading every preset in the pack.
    Q_PROPERTY(QString textureList READ textureList WRITE setTextureList NOTIFY libraryChanged)
    // Where images actually live on this machine. Listing it is one readdir, so "is this
    // preset's image present" is answered exactly, every launch, rather than assumed.
    Q_PROPERTY(QString texturesPath READ texturesPath WRITE setTexturesPath NOTIFY libraryChanged)
    // Where a texture should be dropped to fix one, named in full so the tooltip can say it.
    Q_PROPERTY(QString textureDropPath READ textureDropPath CONSTANT)
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
        BrokenRole,                   // renders nothing, and a texture does not help
        NeedsTextureRole,             // renders nothing only because a texture is missing
        UsesTextureRole,              // renders, but an image it asks for is absent
    };

    explicit PresetLibrary(QObject *parent = nullptr);

    QString rootPath() const { return m_rootPath; }
    void setRootPath(const QString &path);
    QString curatedList() const { return m_curatedList; }
    void setCuratedList(const QString &path);
    QString blocklist() const { return m_blocklist; }
    void setBlocklist(const QString &path);
    QString textureBlocklist() const { return m_textureBlocklist; }
    void setTextureBlocklist(const QString &path);
    QString textureList() const { return m_textureList; }
    void setTextureList(const QString &path);
    QString texturesPath() const { return m_texturesPath; }
    void setTexturesPath(const QString &path);
    // Re-reads the texture directory and updates the marks in place. Cheap - no reindexing - so
    // it can run whenever someone might have just dropped a file in.
    Q_INVOKABLE void refreshTextures();
    QString textureDropPath() const;

    // The external textures a preset asks for, parsed on demand rather than at index time:
    // reading 9,795 files to build the list would cost far more than the 115ms indexing takes,
    // and a tooltip needs it for one row at a time.
    Q_INVOKABLE QStringList texturesWantedBy(const QString &path) const;
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
        bool broken = false;
        bool needsTexture = false;
        bool usesTexture = false;   // wants an image that is not installed
        QStringList wantedTextures;
    };

    void rescan();
    void rebuildFilter();

    // Relative paths, as they appear in the blocklist file. Comparing relative keeps one set
    // usable against the curated install and the full pack, which sit at different roots.
    static QSet<QString> readList(const QString &path);
    // path -> the image names it asks for, read once from the shipped list.
    static QHash<QString, QStringList> readTextureList(const QString &path);
    // The image basenames present on this machine, lowercased, without extension.
    QSet<QString> availableTextures() const;
    // Where images are looked for. The resolved path when one existed at startup, otherwise the
    // writable default - because the directory a person creates *while running* is exactly the
    // case this has to cope with, and it did not until it was tested.
    QString effectiveTexturesPath() const;
    static QString defaultTexturesDir();
    static bool satisfied(const QStringList &wanted, const QSet<QString> &available);

    QString m_rootPath;
    QString m_curatedList;
    QString m_blocklist;
    QString m_textureBlocklist;
    QString m_textureList;
    QString m_texturesPath;
    QHash<QString, QStringList> m_wantedTextures;
    QSet<QString> m_availableTextures;
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
