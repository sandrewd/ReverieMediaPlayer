#include "AppInfo.h"

#include <QtGlobal>

#include <gst/gst.h>
#include <projectM-4/version.h>
#include <taglib/taglib.h>

QString AppInfo::version() const
{
    return QStringLiteral(PLAYER_VERSION);
}

QString AppInfo::buildDate() const
{
    return QStringLiteral(PLAYER_BUILD_DATE);
}

QString AppInfo::commit() const
{
    return QStringLiteral(PLAYER_GIT_COMMIT);
}

QString AppInfo::homepage() const
{
    return QStringLiteral(PLAYER_HOMEPAGE);
}

QString AppInfo::qtVersion() const
{
    // Both matter: a Qt 6.4 quirk worked around at build time can behave differently against
    // a newer runtime.
    const QString runtime = QString::fromLatin1(qVersion());
    const QString compiled = QStringLiteral(QT_VERSION_STR);
    return runtime == compiled ? runtime
                               : QStringLiteral("%1 (built against %2)").arg(runtime, compiled);
}

QString AppInfo::gstreamerVersion() const
{
    guint major = 0, minor = 0, micro = 0, nano = 0;
    gst_version(&major, &minor, &micro, &nano);
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(micro);
}

QString AppInfo::projectMVersion() const
{
    const QString vcs = QStringLiteral(PROJECTM_VERSION_VCS);
    return vcs.isEmpty() ? QStringLiteral(PROJECTM_VERSION_STRING)
                         : QStringLiteral("%1 (%2)").arg(QStringLiteral(PROJECTM_VERSION_STRING),
                                                         vcs.left(9));
}

QString AppInfo::license() const
{
    return QStringLiteral(PLAYER_LICENSE);
}

QString AppInfo::taglibVersion() const
{
    return QStringLiteral("%1.%2.%3").arg(TAGLIB_MAJOR_VERSION)
        .arg(TAGLIB_MINOR_VERSION).arg(TAGLIB_PATCH_VERSION);
}
