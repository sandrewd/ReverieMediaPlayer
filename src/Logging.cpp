#include "Logging.h"

// The third argument is the point: the two-argument form of Q_LOGGING_CATEGORY enables every
// level including Debug, so the categories printed by default and nothing was actually quieted.
// Declaring the minimum level as Info leaves qCDebug() silent until QT_LOGGING_RULES asks for it.
Q_LOGGING_CATEGORY(lcRender, "reverie.render", QtInfoMsg)
Q_LOGGING_CATEGORY(lcPreset, "reverie.preset", QtInfoMsg)
