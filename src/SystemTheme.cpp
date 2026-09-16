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
    m_schemeHint = querySchemeHint();
    m_systemIsDark = detectSystemDark();

    // Three ways of noticing a theme change, because no single one is reliable here.
    // Qt delivers a palette change when the platform theme notices; that did not fire on
    // XFCE in practice, so we also watch the desktop setting directly and re-check the
    // palette on a slow timer. Reading the palette is free, so the poll costs nothing.
    if (qApp)
        qApp->installEventFilter(this);

    startMonitor();

    m_poll.setInterval(1500);
    connect(&m_poll, &QTimer::timeout, this, &SystemTheme::redetect);
    m_poll.start();
}

SystemTheme::~SystemTheme()
{
    if (m_monitor.state() != QProcess::NotRunning) {
        m_monitor.terminate();
        m_monitor.waitForFinished(300);
    }
}

void SystemTheme::startMonitor()
{
    // `gsettings monitor` prints a line per change and costs one idle process. If gsettings
    // is not present the process simply fails to start and the timer carries the load.
    connect(&m_monitor, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString output = QString::fromUtf8(m_monitor.readAllStandardOutput());
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            applyMonitorLine(line);
        redetect();
    });
    m_monitor.start(QStringLiteral("gsettings"),
                    {QStringLiteral("monitor"), QStringLiteral("org.gnome.desktop.interface")});
}

void SystemTheme::applyMonitorLine(const QString &line)
{
    // Lines look like: "color-scheme: 'prefer-dark'" or "gtk-theme: 'Mint-L-Dark-Aqua'"
    if (line.startsWith(QStringLiteral("color-scheme"))) {
        if (line.contains(QStringLiteral("prefer-dark")))
            m_schemeHint = 1;
        else if (line.contains(QStringLiteral("prefer-light")))
            m_schemeHint = 0;
        else
            m_schemeHint = -1;  // "default" expresses no preference
    } else if (line.startsWith(QStringLiteral("gtk-theme"))) {
        m_themeNameHint = line.contains(QStringLiteral("dark"), Qt::CaseInsensitive) ? 1 : 0;
    }
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

int SystemTheme::paletteHint()
{
    if (!qApp)
        return -1;
    const QColor window = qApp->palette().color(QPalette::Window);
    const QColor text = qApp->palette().color(QPalette::WindowText);
    if (!window.isValid() || !text.isValid() || window.lightness() == text.lightness())
        return -1;
    return window.lightness() < text.lightness() ? 1 : 0;
}

int SystemTheme::querySchemeHint()
{
    QProcess gsettings;
    gsettings.start(QStringLiteral("gsettings"),
                    {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                     QStringLiteral("color-scheme")});
    if (!gsettings.waitForFinished(500))
        return -1;
    const QString value = QString::fromUtf8(gsettings.readAllStandardOutput());
    if (value.contains(QStringLiteral("prefer-dark")))
        return 1;
    if (value.contains(QStringLiteral("prefer-light")))
        return 0;
    return -1;
}

bool SystemTheme::detectSystemDark() const
{
    // An explicit desktop preference wins: it is the thing the user actually toggled.
    if (m_schemeHint >= 0)
        return m_schemeHint == 1;

    // Otherwise trust the palette Qt will actually paint with, which the qgtk3 platform
    // theme derives from the GTK theme.
    const int fromPalette = paletteHint();
    if (fromPalette >= 0)
        return fromPalette == 1;

    if (m_themeNameHint >= 0)
        return m_themeNameHint == 1;

    // A visualiser is the centrepiece here, so when nothing can be determined, dark.
    return true;
}
