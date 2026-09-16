#include "PresetLibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

PresetLibrary::PresetLibrary(QObject *parent)
    : QObject(parent)
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

int PresetLibrary::count() const
{
    int total = 0;
    for (const auto &items : m_byCategory)
        total += items.size();
    return total;
}

void PresetLibrary::rescan()
{
    m_byCategory.clear();
    m_categories.clear();

    if (m_rootPath.isEmpty()) {
        emit libraryChanged();
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

    for (const QString &relative : std::as_const(relativePaths)) {
        const int slash = relative.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            continue;
        const QString category = relative.left(slash);
        // Transition effects are not standalone visuals and should not appear in a picker.
        if (category.startsWith(QLatin1Char('!')))
            continue;
        m_byCategory[category].append({QFileInfo(relative).completeBaseName(),
                                       root.absoluteFilePath(relative)});
    }

    for (auto &items : m_byCategory) {
        std::sort(items.begin(), items.end(),
                  [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                      return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
                  });
    }

    m_categories = m_byCategory.keys();
    emit libraryChanged();
}

QVariantList PresetLibrary::presets(const QString &category) const
{
    QVariantList result;
    const auto it = m_byCategory.constFind(category);
    if (it == m_byCategory.constEnd())
        return result;
    result.reserve(it->size());
    for (const auto &entry : *it)
        result.append(QVariantMap{{QStringLiteral("name"), entry.first},
                                  {QStringLiteral("path"), entry.second}});
    return result;
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
