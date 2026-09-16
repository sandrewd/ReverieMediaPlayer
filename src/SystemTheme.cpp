#include "SystemTheme.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QProcess>
#include <QSettings>

SystemTheme::SystemTheme(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_preference = static_cast<Preference>(
        settings.value(QStringLiteral("appearance/preference"), int(FollowSystem)).toInt());
    m_systemIsDark = detectSystemDark();

    // The desktop can change theme while we are running, and Qt delivers that as an
    // application palette change.
    if (qApp)
        qApp->installEventFilter(this);
}

bool SystemTheme::dark() const
{
    switch (m_preference) {
    case AlwaysDark:  return true;
    case AlwaysLight: return false;
    default:          return m_systemIsDark;
    }
}

void SystemTheme::setPreference(Preference preference)
{
    if (preference == m_preference)
        return;
    m_preference = preference;
    QSettings().setValue(QStringLiteral("appearance/preference"), int(preference));
    emit preferenceChanged();
    emit darkChanged();
}

bool SystemTheme::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange)
        redetect();
    return QObject::eventFilter(watched, event);
}

void SystemTheme::redetect()
{
    const bool detected = detectSystemDark();
    if (detected == m_systemIsDark)
        return;
    m_systemIsDark = detected;
    emit systemIsDarkChanged();
    if (m_preference == FollowSystem)
        emit darkChanged();
}

bool SystemTheme::detectSystemDark()
{
    // The qgtk3 platform theme plugin is present on the target distros, so Qt's palette
    // already reflects the GTK theme. A dark window colour is the most direct signal there is.
    if (qApp) {
        const QColor window = qApp->palette().color(QPalette::Window);
        const QColor text = qApp->palette().color(QPalette::WindowText);
        if (window.isValid() && text.isValid() && window.lightness() != text.lightness())
            return window.lightness() < text.lightness();
    }

    // Fallback for desktops where Qt falls back to its own default palette. XFCE and
    // Cinnamon both honour the freedesktop colour-scheme key.
    QProcess gsettings;
    gsettings.start(QStringLiteral("gsettings"),
                    {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                     QStringLiteral("color-scheme")});
    if (gsettings.waitForFinished(500)) {
        const QString value = QString::fromUtf8(gsettings.readAllStandardOutput());
        if (value.contains(QStringLiteral("dark"), Qt::CaseInsensitive))
            return true;
        if (value.contains(QStringLiteral("light"), Qt::CaseInsensitive)
            || value.contains(QStringLiteral("default"), Qt::CaseInsensitive))
            return false;
    }

    // A visualiser is the centrepiece here, so when nothing can be determined, dark.
    return true;
}
