#pragma once

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

public:
    enum Preference { FollowSystem, AlwaysDark, AlwaysLight };
    Q_ENUM(Preference)

    explicit SystemTheme(QObject *parent = nullptr);
    ~SystemTheme() override;

    bool dark() const;
    bool systemIsDark() const { return m_systemIsDark; }
    Preference preference() const { return m_preference; }
    void setPreference(Preference preference);

signals:
    void darkChanged();
    void systemIsDarkChanged();
    void preferenceChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void redetect();
    void startMonitor();
    void applyMonitorLine(const QString &line);
    bool detectSystemDark() const;
    // -1 unknown, 0 light, 1 dark
    static int paletteHint();
    static int querySchemeHint();

    bool m_systemIsDark = true;
    Preference m_preference = FollowSystem;

    // The desktop's own colour-scheme setting, when it expresses a preference at all.
    int m_schemeHint = -1;
    // Fallback read of the GTK theme name, for desktops that only change that.
    int m_themeNameHint = -1;

    QProcess m_monitor;
    QTimer m_poll;
};
