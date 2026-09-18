#pragma once

#include <QColor>
#include <QPalette>
#include <QObject>
#include <QStringList>
#include <QVariantList>
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
    // A fifth role, added 2026-09-17 at the user's request and against the earlier note that the
    // four never grow. The visualiser stage is most of the window and is black when nothing is
    // playing, which on a black desktop leaves the window with no visible edge at all. It is a
    // real surface, so it belongs in the palette rather than in a settings panel of its own.
    Q_PROPERTY(QColor customStage READ customStage WRITE setCustomStage NOTIFY customChanged)

    // Panel sizes live here because this is the singleton that already owns QSettings, and a
    // second one for two integers would be worse. They are clamped in the setters rather than in
    // QML, so a stored value from a bigger screen cannot leave a panel unusable on a smaller one.
    // How strongly the stage colour tints the visualiser while it is running, 0-100. Zero is
    // the default and means the artwork is untouched, which is what it looked like before the
    // colour existed. Idle always shows the colour solid, whatever this says - that is the case
    // the role was added for.
    Q_PROPERTY(int stageTint READ stageTint WRITE setStageTint NOTIFY customChanged)
    Q_PROPERTY(int playlistWidth READ playlistWidth WRITE setPlaylistWidth NOTIFY layoutChanged)
    Q_PROPERTY(int transportHeight READ transportHeight WRITE setTransportHeight NOTIFY layoutChanged)
    Q_PROPERTY(QStringList recentColours READ recentColours NOTIFY recentColoursChanged)
    Q_PROPERTY(int recentColoursLimit READ recentColoursLimit CONSTANT)
    Q_PROPERTY(QVariantList savedPalettes READ savedPalettes NOTIFY savedPalettesChanged)
    Q_PROPERTY(int paletteNameMaximum READ paletteNameLimit CONSTANT)

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
    QColor customStage() const { return m_customStage; }
    int stageTint() const { return m_stageTint; }
    void setStageTint(int percent);
    int playlistWidth() const { return m_playlistWidth; }
    void setPlaylistWidth(int width);
    int transportHeight() const { return m_transportHeight; }
    void setTransportHeight(int height);

    // Neither panel may be squashed away: below these a control is a target nobody can hit.
    static constexpr int kMinPlaylistWidth = 200;
    static constexpr int kMinStageWidth = 360;
    static constexpr int kMinTransportHeight = 64;
    static constexpr int kMaxTransportHeight = 190;
    QStringList recentColours() const { return m_recentColours; }
    int recentColoursLimit() const { return kRecentLimit; }
    QVariantList savedPalettes() const;
    void setCustomBackground(const QColor &colour);
    void setCustomSurface(const QColor &colour);
    void setCustomAccent(const QColor &colour);
    void setCustomText(const QColor &colour);
    void setCustomStage(const QColor &colour);

    // Seeds the custom colours from whichever theme is showing, so editing starts from
    // something that already works rather than from black.
    Q_INVOKABLE void seedCustomFromCurrent();

    // Applies four colours at once and switches to the custom theme.
    Q_INVOKABLE void applyPalettePreset(const QColor &background, const QColor &surface,
                                        const QColor &accent, const QColor &text,
                                        const QColor &stage);

    // A deliberately short history of colours the user has chosen, newest first. Capped, so
    // it cannot grow without bound; picking a colour already in the list moves it to the
    // front rather than duplicating it.
    Q_INVOKABLE void rememberColour(const QColor &colour);

    // Palettes the user has named and kept. Saving under an existing name replaces it, so the
    // list grows only when a genuinely new name is used.
    Q_INVOKABLE void savePalette(const QString &name);
    Q_INVOKABLE void deletePalette(const QString &name);
    Q_INVOKABLE void applySavedPalette(const QString &name);
    static int paletteNameLimit() { return kPaletteNameLimit; }

signals:
    void darkChanged();
    void systemIsDarkChanged();
    void preferenceChanged();
    void customChanged();
    void layoutChanged();
    void recentColoursChanged();
    void savedPalettesChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onPortalSettingChanged(const QString &nspace, const QString &key);

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
    // The freedesktop settings portal. This is the only signal that survives a Flatpak
    // sandbox, where gsettings reads the sandbox's own empty dconf and answers 'default' no
    // matter what the desktop is set to - which made "follow system" pick light on a dark
    // desktop. Returns 1 dark, 0 light, -1 for no preference or no portal.
    static int queryPortalHint();
    static int querySchemeHint();
    static int queryThemeNameHint();

    bool m_systemIsDark = true;
    Preference m_preference = FollowSystem;

    // The desktop's own colour-scheme setting, when it expresses a preference at all.
    int m_portalHint = -1;
    int m_schemeHint = -1;
    // Fallback read of the GTK theme name, for desktops that only change that.
    int m_themeNameHint = -1;

    QColor m_customBackground;
    QColor m_customSurface;
    QColor m_customAccent;
    QColor m_customText;
    QColor m_customStage;
    int m_stageTint = 0;
    int m_playlistWidth = 340;
    int m_transportHeight = 88;
    static constexpr int kRecentLimit = 5;
    static constexpr int kPaletteNameLimit = 24;
    QStringList m_recentColours;
    // Each entry is name + four colours, unit-separated so a name may contain anything.
    QStringList m_savedPalettes;
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
