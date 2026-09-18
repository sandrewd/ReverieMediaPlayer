#pragma once

#include <QLoggingCategory>

// Diagnostics that are useful while working on Reverie and pure noise in a shipped copy.
//
// The frame timer alone emitted a line every thirty frames - about twice a second, for as long as
// the application ran - which fills a user's journal and tells them nothing. Rather than delete
// the instrumentation, which §6 makes load-bearing for this project, it lives in categories that
// are off by default and can be turned on without a rebuild:
//
//     QT_LOGGING_RULES='reverie.render.debug=true' reverie
//     QT_LOGGING_RULES='reverie.*.debug=true' reverie
//
// What stays on unconditionally is the once-per-event kind: which preset directory was found, what
// the audio chain did, which sink was chosen, and every warning. Those are the lines that answer a
// bug report, and there are only a handful of them per run.
Q_DECLARE_LOGGING_CATEGORY(lcRender)
Q_DECLARE_LOGGING_CATEGORY(lcPreset)
