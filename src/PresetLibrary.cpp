#include "PresetLibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
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

void PresetLibrary::setBlocklist(const QString &path)
{
    if (path == m_blocklist)
        return;
    m_blocklist = path;
    rescan();
}

void PresetLibrary::setTextureBlocklist(const QString &path)
{
    if (path == m_textureBlocklist)
        return;
    m_textureBlocklist = path;
    rescan();
}

void PresetLibrary::setTextureList(const QString &path)
{
    if (path == m_textureList)
        return;
    m_textureList = path;
    rescan();
}

QString PresetLibrary::textureDropPath() const
{
    // The writable one, whatever else is on the search path: this is where a person is being
    // told to put a file, so naming a read-only /usr/share would be worse than saying nothing.
    const QString path = defaultTexturesDir();
    // Abbreviated for display, which is all this is for. An unbroken absolute path is also a
    // single very long word, and a tooltip sizes itself to its longest one - it covered the list
    // it was describing before this.
    const QString home = QDir::homePath();
    if (!home.isEmpty() && path.startsWith(home))
        return QStringLiteral("~") + path.mid(home.length());
    return path;
}

// The names come from the shipped list rather than from parsing the preset here. Parsing was the
// first design and it was wrong twice: on hover it is fine, but the *marks* need the answer for
// every row, and working that out means reading every preset in the pack - 3.35s from a cold
// cache against 115ms for the whole index.
QStringList PresetLibrary::texturesWantedBy(const QString &path) const
{
    const QDir root(m_rootPath);
    return m_wantedTextures.value(root.relativeFilePath(path));
}

// projectM reads "randNN" as "any image from the pool" and "randNN_prefix" as "any image whose
// filename begins with prefix" - see TextureManager::GetRandomTexture. The tail is not a filename,
// so it is neither looked up nor reported as one.
bool PresetLibrary::satisfied(const QStringList &wanted, const QSet<QString> &available)
{
    static const QRegularExpression randomName(QStringLiteral("^rand\\d+(?:_(.+))?$"));
    for (const QString &name : wanted) {
        const QRegularExpressionMatch random = randomName.match(name);
        if (random.hasMatch()) {
            const QString prefix = random.captured(1);
            if (available.isEmpty())
                return false;
            if (prefix.isEmpty())
                continue;  // any image will do, and there is at least one
            bool any = false;
            for (const QString &have : available) {
                if (have.startsWith(prefix)) { any = true; break; }
            }
            if (!any)
                return false;
        } else if (!available.contains(name)) {
            return false;
        }
    }
    return true;
}

QString PresetLibrary::defaultTexturesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/reverie/textures");
}

QString PresetLibrary::effectiveTexturesPath() const
{
    return m_texturesPath.isEmpty() ? defaultTexturesDir() : m_texturesPath;
}

QSet<QString> PresetLibrary::availableTextures() const
{
    QSet<QString> available;
    const QString dir = effectiveTexturesPath();
    if (dir.isEmpty() || !QFileInfo::exists(dir))
        return available;
    // One readdir. projectM accepts what stb_image can decode; listing everything and letting the
    // name match is closer to what it actually does than second-guessing the extension.
    QDirIterator it(dir, QDir::Files);
    while (it.hasNext()) {
        it.next();
        available.insert(it.fileInfo().completeBaseName().toLower());
    }
    return available;
}

QHash<QString, QStringList> PresetLibrary::readTextureList(const QString &path)
{
    QHash<QString, QStringList> wanted;
    if (path.isEmpty())
        return wanted;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("preset library: cannot read texture list %s", qPrintable(path));
        return wanted;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0)
            continue;
        wanted.insert(line.left(tab),
                      line.mid(tab + 1).split(QLatin1Char(','), Qt::SkipEmptyParts));
    }
    return wanted;
}

void PresetLibrary::refreshTextures()
{
    if (m_rootPath.isEmpty() || m_textureList.isEmpty())
        return;
    // Free when nothing has changed, which is almost always. Comparing the directory listing is
    // one readdir; only a genuine change pays for a rescan.
    //
    // A full rescan rather than a flag update, because availability decides more than the mark:
    // a preset whose image has just appeared stops being excluded, so it has to rejoin the list
    // and the rotation. Updating the flags alone would leave it marked correctly and still
    // hidden, which is a worse state than either.
    const QSet<QString> available = availableTextures();
    if (available == m_availableTextures)
        return;
    qInfo("texture library: %d image(s) available, re-checking visualisations",
          int(available.size()));
    rescan();
}

void PresetLibrary::setShowBroken(bool show)
{
    if (show == m_showBroken)
        return;
    m_showBroken = show;
    rescan();
}

void PresetLibrary::setTexturesPath(const QString &path)
{
    if (path == m_texturesPath)
        return;
    m_texturesPath = path;
    rescan();
}

QSet<QString> PresetLibrary::readList(const QString &path)
{
    QSet<QString> entries;
    if (path.isEmpty())
        return entries;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Worth saying out loud: without it the library silently gains a couple of hundred
        // presets that draw nothing, which reads as a rendering fault rather than a missing file.
        qWarning("preset library: cannot read blocklist %s", qPrintable(path));
        return entries;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
            entries.insert(line);
    }
    return entries;
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
    case BrokenRole:        return e.broken;
    case NeedsTextureRole:  return e.needsTexture;
    case UsesTextureRole:   return e.usesTexture;
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
            {PathRole, "path"},
            {BrokenRole, "broken"},
            {NeedsTextureRole, "needsTexture"},
            {UsesTextureRole, "usesTexture"}};
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

    const QSet<QString> broken = readList(m_blocklist);
    const QSet<QString> needsTexture = readList(m_textureBlocklist);
    m_wantedTextures = readTextureList(m_textureList);
    m_availableTextures = availableTextures();
    const QSet<QString> &available = m_availableTextures;
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
        const bool isBroken = broken.contains(relative);
        // Per preset rather than all-or-nothing: one of these is only broken while the image it
        // names is actually absent, and someone with a partial texture pack has some of them
        // working and some not.
        const bool isTextureless =
            needsTexture.contains(relative)
            && !satisfied(m_wantedTextures.value(relative), available);
        if (isBroken || isTextureless) {
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
        const QStringList wanted = m_wantedTextures.value(relative);
        m_all.append({name, absolute, category, style == QLatin1String(".") ? QString() : style,
                      isBroken, isTextureless,
                      !wanted.isEmpty() && !satisfied(wanted, available), wanted});
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
