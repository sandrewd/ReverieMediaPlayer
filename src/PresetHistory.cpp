#include "PresetHistory.h"

#include <QFileInfo>
#include <QSettings>

namespace {
// Ten is what a menu can hold without becoming a list to read rather than a thing to point at.
constexpr int kMaxRecents = 10;

QString titleOf(const QString &name)
{
    const int sep = name.indexOf(QStringLiteral(" - "));
    return sep > 0 ? name.mid(sep + 3).trimmed() : name;
}
} // namespace

PresetHistory::PresetHistory(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_favourites = settings.value(QStringLiteral("visualisation/favourites")).toStringList();
    m_recents = settings.value(QStringLiteral("visualisation/recents")).toStringList();
    if (m_recents.size() > kMaxRecents)
        m_recents = m_recents.mid(0, kMaxRecents);
}

void PresetHistory::save()
{
    QSettings settings;
    settings.setValue(QStringLiteral("visualisation/favourites"), m_favourites);
    settings.setValue(QStringLiteral("visualisation/recents"), m_recents);
}

QVariantList PresetHistory::describe(const QStringList &paths) const
{
    QVariantList out;
    for (const QString &p : paths) {
        // A path can stop existing - the extra pack is removable, and a favourite taken from it
        // should reappear if it is installed again rather than be silently deleted here. So the
        // entry is kept in settings and skipped for display.
        if (!QFileInfo::exists(p))
            continue;
        const QString name = QFileInfo(p).completeBaseName();
        out.append(QVariantMap{{QStringLiteral("name"), name},
                               {QStringLiteral("title"), titleOf(name)},
                               {QStringLiteral("path"), p}});
    }
    return out;
}

QVariantList PresetHistory::favourites() const
{
    return describe(m_favourites);
}

QVariantList PresetHistory::recents() const
{
    return describe(m_recents);
}

int PresetHistory::favouriteCount() const
{
    return int(favourites().size());
}

bool PresetHistory::isFavourite(const QString &path) const
{
    return !path.isEmpty() && m_favourites.contains(path);
}

void PresetHistory::toggleFavourite(const QString &path)
{
    if (path.isEmpty())
        return;
    if (m_favourites.removeAll(path) == 0)
        m_favourites.prepend(path);
    save();
    emit favouritesChanged();
}

void PresetHistory::noteUsed(const QString &path)
{
    if (path.isEmpty())
        return;
    // Moving an existing entry to the front rather than duplicating it, so a preset that keeps
    // coming round does not fill the whole list with itself.
    m_recents.removeAll(path);
    m_recents.prepend(path);
    while (m_recents.size() > kMaxRecents)
        m_recents.removeLast();
    save();
    emit recentsChanged();
}

void PresetHistory::clearRecents()
{
    if (m_recents.isEmpty())
        return;
    m_recents.clear();
    save();
    emit recentsChanged();
}
