#include "PresetLibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

PresetLibrary::PresetLibrary(QObject *parent)
    : QAbstractListModel(parent)
{
}

void PresetLibrary::setRootPath(const QString &path)
{
    if (path == m_rootPath)
        return;
    m_rootPath = path;
    rescan();
}

void PresetLibrary::setCuratedList(const QString &path)
{
    if (path == m_curatedList)
        return;
    m_curatedList = path;
    rescan();
}

void PresetLibrary::setBlocklists(const QStringList &paths)
{
    if (paths == m_blocklists)
        return;
    m_blocklists = paths;
    rescan();
}

void PresetLibrary::setShowBroken(bool show)
{
    if (show == m_showBroken)
        return;
    m_showBroken = show;
    rescan();
}

QSet<QString> PresetLibrary::readBlocklist() const
{
    QSet<QString> blocked;
    for (const QString &path : m_blocklists) {
        if (path.isEmpty())
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Worth saying out loud: without it the library silently gains a couple of hundred
            // presets that draw nothing, which reads as a rendering fault rather than a missing
            // file.
            qWarning("preset library: cannot read blocklist %s", qPrintable(path));
            continue;
        }
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
                blocked.insert(line);
        }
    }
    return blocked;
}

void PresetLibrary::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    rebuildFilter();
}

void PresetLibrary::setFilterCategory(const QString &category)
{
    if (m_filterCategory == category)
        return;
    m_filterCategory = category;
    rebuildFilter();
}

void PresetLibrary::setFilterStyle(const QString &style)
{
    if (m_filterStyle == style)
        return;
    m_filterStyle = style;
    rebuildFilter();
}

void PresetLibrary::clearFilters()
{
    if (m_searchText.isEmpty() && m_filterCategory.isEmpty() && m_filterStyle.isEmpty())
        return;
    m_searchText.clear();
    m_filterCategory.clear();
    m_filterStyle.clear();
    rebuildFilter();
}

int PresetLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_filtered.size());
}

QVariant PresetLibrary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filtered.size())
        return {};
    const Entry &e = m_all.at(m_filtered.at(index.row()));
    switch (role) {
    case NameRole:     return e.name;
    case TitleRole:    return titleOf(e.name);
    case AuthorRole:   return artistOf(e.name);
    case CategoryRole: return e.category;
    case StyleRole:    return e.style;
    case PathRole:     return e.path;
    default:           return {};
    }
}

QHash<int, QByteArray> PresetLibrary::roleNames() const
{
    return {{NameRole, "name"},
            {TitleRole, "title"},
            {AuthorRole, "author"},
            {CategoryRole, "category"},
            {StyleRole, "style"},
            {PathRole, "path"}};
}

QStringList PresetLibrary::styles(const QString &category) const
{
    QStringList result;
    for (const Entry &e : m_all) {
        if (e.style.isEmpty())
            continue;
        if (!category.isEmpty() && e.category != category)
            continue;
        if (!result.contains(e.style))
            result.append(e.style);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList PresetLibrary::filteredPaths() const
{
    QStringList result;
    result.reserve(m_filtered.size());
    for (int i : m_filtered)
        result.append(m_all.at(i).path);
    return result;
}

void PresetLibrary::rebuildFilter()
{
    beginResetModel();
    m_filtered.clear();
    // Matched against the name, the author and the style, because a person typing "rorschach"
    // is as likely to mean the style as a preset whose title contains it, and typing an author
    // should find their work. Cheap enough at 9,795 entries to do on every keystroke.
    const QString needle = m_searchText.trimmed();
    for (int i = 0; i < m_all.size(); ++i) {
        const Entry &e = m_all.at(i);
        if (!m_filterCategory.isEmpty() && e.category != m_filterCategory)
            continue;
        if (!m_filterStyle.isEmpty() && e.style != m_filterStyle)
            continue;
        if (!needle.isEmpty()
            && !e.name.contains(needle, Qt::CaseInsensitive)
            && !e.style.contains(needle, Qt::CaseInsensitive)
            && !e.category.contains(needle, Qt::CaseInsensitive))
            continue;
        m_filtered.append(i);
    }
    endResetModel();
    emit filterChanged();
}

void PresetLibrary::rescan()
{
    m_byCategory.clear();
    m_categories.clear();

    if (m_rootPath.isEmpty()) {
        m_all.clear();
        emit libraryChanged();
        rebuildFilter();
        return;
    }

    const QDir root(m_rootPath);
    QStringList relativePaths;

    if (!m_curatedList.isEmpty()) {
        QFile file(m_curatedList);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
                    relativePaths.append(line);
            }
        }
    }

    if (relativePaths.isEmpty()) {
        QDirIterator it(m_rootPath, {QStringLiteral("*.milk")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            relativePaths.append(root.relativeFilePath(it.next()));
    }

    const QSet<QString> blocked = readBlocklist();
    m_brokenCount = 0;

    m_all.clear();
    for (const QString &relative : std::as_const(relativePaths)) {
        const int slash = relative.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            continue;
        const QString category = relative.left(slash);
        // Transition effects are not standalone visuals and should not appear in a picker.
        if (category.startsWith(QLatin1Char('!')))
            continue;
        // Measured to render nothing. Counted whether or not they are shown, because the menu
        // item that reveals them needs to say how many there are.
        if (blocked.contains(relative)) {
            ++m_brokenCount;
            if (!m_showBroken)
                continue;
        }
        const QString name = QFileInfo(relative).completeBaseName();
        const QString absolute = root.absoluteFilePath(relative);
        // Everything between the category and the file is the pack's own finer grouping. It is
        // one level deep throughout the corpus, but joining whatever is there keeps this honest
        // if that ever changes.
        const QString dir = QFileInfo(relative).path();
        const QString style = dir.mid(slash + 1);
        m_all.append({name, absolute, category, style == QLatin1String(".") ? QString() : style});
        m_byCategory[category].append({name, absolute});
    }

    for (auto &items : m_byCategory) {
        std::sort(items.begin(), items.end(),
                  [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                      return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
                  });
    }

    m_categories = m_byCategory.keys();
    emit libraryChanged();
    rebuildFilter();
}


QString PresetLibrary::artistOf(const QString &presetName)
{
    const int separator = presetName.indexOf(QStringLiteral(" - "));
    return separator > 0 ? presetName.left(separator).trimmed() : QString();
}

QString PresetLibrary::titleOf(const QString &presetName)
{
    const int separator = presetName.indexOf(QStringLiteral(" - "));
    return separator > 0 ? presetName.mid(separator + 3).trimmed() : presetName;
}



QStringList PresetLibrary::paths(const QString &category) const
{
    QStringList result;
    if (category.isEmpty()) {
        for (const auto &items : m_byCategory)
            for (const auto &entry : items)
                result.append(entry.second);
        return result;
    }
    const auto it = m_byCategory.constFind(category);
    if (it != m_byCategory.constEnd())
        for (const auto &entry : *it)
            result.append(entry.second);
    return result;
}
