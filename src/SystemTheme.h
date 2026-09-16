#pragma once

#include <QColor>
#include <QPalette>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// Qt 6.4 predates Qt::ColorScheme and QStyleHints::colorScheme, both of which arrive in 6.5,
// so light/dark has to be worked out here. Noble ships 6.4.2, so this is not optional.
class SystemTheme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(bool systemIsDark READ systemIsDark NOTIFY systemIsDarkChanged)
    Q_PROPERTY(Preference preference READ preference WRITE setPreference NOTIFY preferenceChanged)

    // Four colours is the whole customisation surface. Everything else - hover states,
    // borders, dimmed text - is derived, so a user cannot build a theme where text is
    // invisible against its own background.
    Q_PROPERTY(QColor customBackground READ customBackground WRITE setCustomBackground NOTIFY customChanged)
    Q_PROPERTY(QColor customSurface READ customSurface WRITE setCustomSurface NOTIFY customChanged)
    Q_PROPERTY(QColor customAccent READ customAccent WRITE setCustomAccent NOTIFY customChanged)
    Q_PROPERTY(QColor customText READ customText WRITE setCustomText NOTIFY customChanged)

public:
    enum Preference { FollowSystem, AlwaysDark, AlwaysLight, Custom };
    Q_ENUM(Preference)

    explicit SystemTheme(QObject *parent = nullptr);
    ~SystemTheme() override;

    bool dark() const;
    bool systemIsDark() const { return m_systemIsDark; }
    Preference preference() const { return m_preference; }
    void setPreference(Preference preference);

    QColor customBackground() const { return m_customBackground; }
    QColor customSurface() const { return m_customSurface; }
    QColor customAccent() const { return m_customAccent; }
    QColor customText() const { return m_customText; }
    void setCustomBackground(const QColor &colour);
    void setCustomSurface(const QColor &colour);
    void setCustomAccent(const QColor &colour);
    void setCustomText(const QColor &colour);

    // Seeds the custom colours from whichever theme is showing, so editing starts from
    // something that already works rather than from black.
    Q_INVOKABLE void seedCustomFromCurrent();

signals:
    void darkChanged();
    void systemIsDarkChanged();
    void preferenceChanged();
    void customChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Qt Quick Controls - menus, dialogs, buttons - paint from the application palette, not
    // from our QML tokens. Without pushing the theme into the palette a user's colours stop
    // at the edge of every menu.
    void applyPalette();

    void redetect();
    void startMonitor();
    void applyMonitorLine(const QString &line);
    bool detectSystemDark() const;
    // -1 unknown, 0 light, 1 dark
    static int paletteHint();
    static int querySchemeHint();
    static int queryThemeNameHint();

    bool m_systemIsDark = true;
    Preference m_preference = FollowSystem;

    // The desktop's own colour-scheme setting, when it expresses a preference at all.
    int m_schemeHint = -1;
    // Fallback read of the GTK theme name, for desktops that only change that.
    int m_themeNameHint = -1;

    QColor m_customBackground;
    QColor m_customSurface;
    QColor m_customAccent;
    QColor m_customText;
    // The desktop's own palette, captured before we ever impose one. Qt has no API to read
    // the platform palette back once an application palette has been set, so this is the only
    // chance to have it.
    QPalette m_platformPalette;
    int m_platformPaletteIsDark = -1;
    bool m_applyingPalette = false;
    // Once we set a palette, Qt stops refreshing it from the platform, so the palette is no
    // longer usable as a detection signal.
    bool m_palettePinned = false;

    QProcess m_monitor;
    QProcess m_xfconfMonitor;
    QTimer m_poll;
    QTimer m_resurvey;
};
