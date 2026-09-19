#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Everything the About dialog needs, resolved at build time where possible. The component
// versions matter beyond curiosity: §3's stack is LGPL and dynamically linked, and naming the
// libraries and their versions is the attribution that licensing actually asks for.
class AppInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString buildDate READ buildDate CONSTANT)
    Q_PROPERTY(QString commit READ commit CONSTANT)
    Q_PROPERTY(QString homepage READ homepage CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString gstreamerVersion READ gstreamerVersion CONSTANT)
    Q_PROPERTY(QString projectMVersion READ projectMVersion CONSTANT)
    Q_PROPERTY(QString taglibVersion READ taglibVersion CONSTANT)
    Q_PROPERTY(QString license READ license CONSTANT)

public:
    // One line per geometry change, for chasing resize behaviour that only shows up on someone
    // else's compositor. Off unless QT_LOGGING_RULES asks for reverie.layout.debug.
    Q_INVOKABLE void logLayout(const QString &what, int x, int y, int w, int h,
                               int minH, int maxH, qreal scale) const;

    explicit AppInfo(QObject *parent = nullptr) : QObject(parent) {}

    QString version() const;
    QString buildDate() const;
    QString commit() const;
    QString homepage() const;
    QString qtVersion() const;
    QString gstreamerVersion() const;
    QString projectMVersion() const;
    QString taglibVersion() const;
    QString license() const;
};
