#include "SystemTheme.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QProcess>
#include <QGuiApplication>
#include <QSettings>

SystemTheme::SystemTheme(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_preference = static_cast<Preference>(
        settings.value(QStringLiteral("appearance/preference"), int(FollowSystem)).toInt());
    const auto readColour = [&settings](const char *key, const char *fallback) {
        // Accepts both the "#rrggbb" we write now and a QVariant-serialised QColor from an
        // older settings file.
        const QVariant stored = settings.value(QLatin1String(key));
        if (stored.typeId() == QMetaType::QColor)
            return stored.value<QColor>();
        const QColor parsed(stored.toString());
        return parsed.isValid() ? parsed : QColor(QLatin1String(fallback));
    };
    m_customBackground = readColour("appearance/background", "#0f1115");
    m_customSurface = readColour("appearance/surface", "#171a21");
    m_customAccent = readColour("appearance/accent", "#4da3ff");
    m_customText = readColour("appearance/text", "#e8eaed");

    if (qApp) {
        m_platformPalette = qApp->palette();
        m_platformPaletteIsDark = paletteHint();
    }

    m_recentColours = settings.value(QStringLiteral("appearance/recentColours")).toStringList();
    while (m_recentColours.size() > kRecentLimit)
        m_recentColours.removeLast();

    m_savedPalettes = settings.value(QStringLiteral("appearance/savedPalettes")).toStringList();

    m_schemeHint = querySchemeHint();
    m_themeNameHint = queryThemeNameHint();
    m_systemIsDark = detectSystemDark();
    qInfo("theme at startup: %s  (scheme hint %d, theme-name hint %d, palette hint %d)",
          m_systemIsDark ? "dark" : "light", m_schemeHint, m_themeNameHint,
          m_platformPaletteIsDark);

    // Only impose a palette when the user has actually chosen one. Calling setPalette() at
    // all marks it explicitly set, after which Qt no longer refreshes it from the platform
    // theme - which would silently break following the system.
    if (m_preference != FollowSystem)
        applyPalette();

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

    m_resurvey.setInterval(4000);
    connect(&m_resurvey, &QTimer::timeout, this, [this]() {
        const int scheme = querySchemeHint();
        const int name = queryThemeNameHint();
        if (scheme == m_schemeHint && name == m_themeNameHint)
            return;
        m_schemeHint = scheme;
        m_themeNameHint = name;
        redetect();
    });
    m_resurvey.start();
}

SystemTheme::~SystemTheme()
{
    m_resurvey.stop();
    for (QProcess *process : {&m_monitor, &m_xfconfMonitor}) {
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            process->waitForFinished(300);
        }
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

    // XFCE's own theme setting, which its appearance dialog writes and which gsettings may
    // never see.
    connect(&m_xfconfMonitor, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString output = QString::fromUtf8(m_xfconfMonitor.readAllStandardOutput());
        if (output.contains(QStringLiteral("dark"), Qt::CaseInsensitive))
            m_themeNameHint = 1;
        else if (!output.trimmed().isEmpty())
            m_themeNameHint = 0;
        redetect();
    });
    m_xfconfMonitor.start(QStringLiteral("xfconf-query"),
                          {QStringLiteral("-c"), QStringLiteral("xsettings"),
                           QStringLiteral("-p"), QStringLiteral("/Net/ThemeName"),
                           QStringLiteral("-m")});
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
    // A custom scheme is dark or light according to its own background, which is what the
    // derived tokens need to know.
    case Custom:      return m_customBackground.lightness() < 128;
    default:          return m_systemIsDark;
    }
}

void SystemTheme::setCustomBackground(const QColor &colour)
{
    if (!colour.isValid() || colour == m_customBackground)
        return;
    m_customBackground = colour;
    QSettings().setValue(QStringLiteral("appearance/background"), colour.name());
    applyPalette();
    emit customChanged();
    emit darkChanged();
}

void SystemTheme::setCustomSurface(const QColor &colour)
{
    if (!colour.isValid() || colour == m_customSurface)
        return;
    m_customSurface = colour;
    QSettings().setValue(QStringLiteral("appearance/surface"), colour.name());
    applyPalette();
    emit customChanged();
}

void SystemTheme::setCustomAccent(const QColor &colour)
{
    if (!colour.isValid() || colour == m_customAccent)
        return;
    m_customAccent = colour;
    QSettings().setValue(QStringLiteral("appearance/accent"), colour.name());
    applyPalette();
    emit customChanged();
}

void SystemTheme::setCustomText(const QColor &colour)
{
    if (!colour.isValid() || colour == m_customText)
        return;
    m_customText = colour;
    QSettings().setValue(QStringLiteral("appearance/text"), colour.name());
    applyPalette();
    emit customChanged();
}

void SystemTheme::applyPalettePreset(const QColor &background, const QColor &surface,
                                     const QColor &accent, const QColor &text)
{
    setCustomBackground(background);
    setCustomSurface(surface);
    setCustomAccent(accent);
    setCustomText(text);
    setPreference(Custom);
}

void SystemTheme::rememberColour(const QColor &colour)
{
    if (!colour.isValid())
        return;
    const QString name = colour.name();

    // Moving an existing entry to the front keeps the list useful: five slots filled with the
    // same colour would be worse than useless.
    m_recentColours.removeAll(name);
    m_recentColours.prepend(name);
    while (m_recentColours.size() > kRecentLimit)
        m_recentColours.removeLast();

    QSettings().setValue(QStringLiteral("appearance/recentColours"), m_recentColours);
    emit recentColoursChanged();
}

QVariantList SystemTheme::savedPalettes() const
{
    QVariantList result;
    for (const QString &entry : m_savedPalettes) {
        const QStringList parts = entry.split(QLatin1Char('\x1f'));
        if (parts.size() != 5)
            continue;
        result.append(QVariantMap{{QStringLiteral("name"), parts.at(0)},
                                  {QStringLiteral("background"), parts.at(1)},
                                  {QStringLiteral("surface"), parts.at(2)},
                                  {QStringLiteral("accent"), parts.at(3)},
                                  {QStringLiteral("text"), parts.at(4)}});
    }
    return result;
}

void SystemTheme::savePalette(const QString &name)
{
    const QString trimmed = name.trimmed().left(kPaletteNameLimit);
    if (trimmed.isEmpty())
        return;

    const QString entry = QStringList{trimmed, m_customBackground.name(), m_customSurface.name(),
                                      m_customAccent.name(), m_customText.name()}
                              .join(QLatin1Char('\x1f'));

    // Saving under a name that already exists replaces it rather than making a duplicate.
    for (int i = 0; i < m_savedPalettes.size(); ++i) {
        if (m_savedPalettes.at(i).section(QLatin1Char('\x1f'), 0, 0).compare(
                trimmed, Qt::CaseInsensitive) == 0) {
            m_savedPalettes[i] = entry;
            QSettings().setValue(QStringLiteral("appearance/savedPalettes"), m_savedPalettes);
            emit savedPalettesChanged();
            return;
        }
    }

    m_savedPalettes.append(entry);
    QSettings().setValue(QStringLiteral("appearance/savedPalettes"), m_savedPalettes);
    emit savedPalettesChanged();
}

void SystemTheme::deletePalette(const QString &name)
{
    const int before = m_savedPalettes.size();
    m_savedPalettes.removeIf([&name](const QString &entry) {
        return entry.section(QLatin1Char('\x1f'), 0, 0).compare(name, Qt::CaseInsensitive) == 0;
    });
    if (m_savedPalettes.size() == before)
        return;
    QSettings().setValue(QStringLiteral("appearance/savedPalettes"), m_savedPalettes);
    emit savedPalettesChanged();
}

void SystemTheme::applySavedPalette(const QString &name)
{
    for (const QString &entry : std::as_const(m_savedPalettes)) {
        const QStringList parts = entry.split(QLatin1Char('\x1f'));
        if (parts.size() != 5 || parts.at(0).compare(name, Qt::CaseInsensitive) != 0)
            continue;
        applyPalettePreset(QColor(parts.at(1)), QColor(parts.at(2)),
                           QColor(parts.at(3)), QColor(parts.at(4)));
        return;
    }
}

void SystemTheme::seedCustomFromCurrent()
{
    const bool isDark = dark();
    setCustomBackground(QColor(isDark ? "#0f1115" : "#f2f3f5"));
    setCustomSurface(QColor(isDark ? "#171a21" : "#ffffff"));
    setCustomAccent(QColor(isDark ? "#4da3ff" : "#1f6feb"));
    setCustomText(QColor(isDark ? "#e8eaed" : "#1b1e23"));
}

void SystemTheme::setPreference(Preference preference)
{
    if (preference == m_preference)
        return;
    m_preference = preference;
    QSettings().setValue(QStringLiteral("appearance/preference"), int(preference));
    applyPalette();
    emit preferenceChanged();
    emit darkChanged();
}

bool SystemTheme::eventFilter(QObject *watched, QEvent *event)
{
    // Ignore the palette change we caused ourselves, or we would re-detect from our own
    // output and chase our tail.
    if (event->type() == QEvent::ApplicationPaletteChange && !m_applyingPalette
        && !m_palettePinned) {
        // Only meaningful while the platform still owns the palette.
        if (qApp) {
            m_platformPalette = qApp->palette();
            m_platformPaletteIsDark = paletteHint();
        }
        redetect();
    }
    return QObject::eventFilter(watched, event);
}

void SystemTheme::applyPalette()
{
    if (!qApp)
        return;

    m_applyingPalette = true;

    const bool isDark = dark();

    if (m_preference == FollowSystem) {
        // Qt cannot un-set an application palette, so returning to Follow system after a
        // custom theme has to mean building one rather than clearing one. Where the desktop
        // has not changed polarity since launch, the palette captured at startup is the real
        // thing and gets used verbatim - exact GTK colours, not an approximation. If it has
        // changed, fall through and construct one, which gets the polarity right even though
        // it will not match an unusual GTK theme shade for shade.
        if (m_platformPaletteIsDark >= 0 && (m_platformPaletteIsDark == 1) == isDark) {
            qApp->setPalette(m_platformPalette);
            m_palettePinned = true;
            m_applyingPalette = false;
            return;
        }
    }
    const QColor background = m_preference == Custom ? m_customBackground
                                                     : QColor(isDark ? "#0f1115" : "#f2f3f5");
    const QColor surface = m_preference == Custom ? m_customSurface
                                                  : QColor(isDark ? "#171a21" : "#ffffff");
    const QColor text = m_preference == Custom ? m_customText
                                               : QColor(isDark ? "#e8eaed" : "#1b1e23");
    const QColor accent = m_preference == Custom ? m_customAccent
                                                 : QColor(isDark ? "#4da3ff" : "#1f6feb");

    QColor dimText = text;
    dimText.setAlphaF(0.62f);

    QPalette palette;
    palette.setColor(QPalette::Window, background);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, surface);
    palette.setColor(QPalette::AlternateBase, isDark ? surface.lighter(120) : surface.darker(104));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, surface);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::ToolTipBase, surface);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText,
                     accent.lightness() > 140 ? QColor("#0f1115") : QColor("#f2f4f6"));
    palette.setColor(QPalette::PlaceholderText, dimText);
    palette.setColor(QPalette::Disabled, QPalette::Text, dimText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, dimText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, dimText);

    qApp->setPalette(palette);
    m_palettePinned = true;
    m_applyingPalette = false;
}

void SystemTheme::redetect()
{
    const bool detected = detectSystemDark();
    if (detected == m_systemIsDark)
        return;
    m_systemIsDark = detected;
    qInfo("desktop theme now %s", detected ? "dark" : "light");
    emit systemIsDarkChanged();
    if (m_preference == FollowSystem) {
        // Qt Quick Controls paint from the application palette, so the QML tokens changing is
        // only half of it: menus and dialogs need the palette rebuilt as well.
        applyPalette();
        emit darkChanged();
    }
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

int SystemTheme::queryThemeNameHint()
{
    // XFCE stores the active theme in xfconf and may never touch the gsettings keys, so ask
    // it directly as well.
    for (const auto &probe : {
             std::pair<QString, QStringList>{QStringLiteral("xfconf-query"),
                 {QStringLiteral("-c"), QStringLiteral("xsettings"), QStringLiteral("-p"),
                  QStringLiteral("/Net/ThemeName")}},
             std::pair<QString, QStringList>{QStringLiteral("gsettings"),
                 {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                  QStringLiteral("gtk-theme")}}}) {
        QProcess process;
        process.start(probe.first, probe.second);
        if (!process.waitForFinished(500))
            continue;
        const QString value = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (value.isEmpty())
            continue;
        return value.contains(QStringLiteral("dark"), Qt::CaseInsensitive) ? 1 : 0;
    }
    return -1;
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
    // Order matters here, and it took measurement on Mint XFCE to get right.
    //
    // The Qt palette looks like the strongest signal - it comes from qgtk3 and describes what
    // we will actually paint - but it does NOT refresh when the GTK theme changes on this
    // desktop. Trusting it first made a live switch look like "no change", because the stale
    // palette answered before anything else got a say. It is now the last resort.
    //
    // `color-scheme` cannot carry the light case at all here: switching to a light theme
    // leaves it at 'default' rather than 'prefer-light'. It is authoritative only when it
    // states a preference outright, which is the GNOME case.
    if (m_schemeHint >= 0)
        return m_schemeHint == 1;

    // The theme *name* is the one signal that moves cleanly in both directions on XFCE:
    // Mint-L-Dark-Aqua against Mint-L-Aqua.
    if (m_themeNameHint >= 0)
        return m_themeNameHint == 1;

    if (!m_palettePinned) {
        const int fromPalette = paletteHint();
        if (fromPalette >= 0)
            return fromPalette == 1;
    }

    if (m_platformPaletteIsDark >= 0)
        return m_platformPaletteIsDark == 1;

    // A visualiser is the centrepiece here, so when nothing can be determined, dark.
    return true;
}
