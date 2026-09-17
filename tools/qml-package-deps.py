#!/usr/bin/env python3
"""Work out which Debian packages provide the QML modules this UI loads.

Declaring them by hand does not work, and failed in exactly the way you would expect it to.
`dpkg-shlibdeps` cannot see QML modules because they are resolved by name at runtime and never
linked, so nothing in the ELF headers mentions them. Worse, the obvious dependency does not imply
the others: `qml6-module-qtquick-controls` depends on the *library* `libqt6quicktemplates2-6` but
not on `qml6-module-qtquick-templates`, the QML module. A developer machine has the module anyway,
pulled in by the Qt dev packages, so the gap is invisible until someone installs the package on a
clean machine and the application dies at QML load with "plugin not found".

So the list is derived, not remembered: qmlimportscanner reports what the UI actually imports,
including transitively, and dpkg says which package ships each one.

Prints one package name per line. Exits non-zero if the tools are unavailable, so the caller can
fall back to a static list rather than silently shipping no QML dependencies at all.
"""

import json
import os
import shutil
import subprocess
import sys

SCANNER_CANDIDATES = [
    "/usr/lib/qt6/libexec/qmlimportscanner",
    "/usr/lib/x86_64-linux-gnu/qt6/libexec/qmlimportscanner",
]

QML_DIR_CANDIDATES = [
    "/usr/lib/x86_64-linux-gnu/qt6/qml",
    "/usr/lib/qt6/qml",
]


# Modules Qt loads at runtime with no import statement anywhere in our QML, which means
# qmlimportscanner cannot see them and the derived list alone ships a broken package.
#
# QtQml.WorkerScript is pulled in behind ListModel. Dropping it produced an application that
# died at QML load with "module QtQml.WorkerScript is not installed", on a machine that had
# every module the scanner named.
#
# To check whether this list is still complete, trace what the running process actually opens:
#
#   strace -f -e trace=openat -o /tmp/t.txt reverie
#   grep -o '"…/qt6/qml/[^"]*qmldir"' /tmp/t.txt | sort -u
#
# Anything there that the scanner does not report belongs in this list. Hiding a module with
# `mv` and restarting is the direct confirmation.
IMPLICIT_MODULES = [
    "QtQml.WorkerScript",
]


def first_existing(paths):
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def main():
    if len(sys.argv) < 2:
        print("usage: qml-package-deps.py <qml-source-dir>", file=sys.stderr)
        return 2
    root = sys.argv[1]

    scanner = first_existing(SCANNER_CANDIDATES) or shutil.which("qmlimportscanner")
    qml_dir = first_existing(QML_DIR_CANDIDATES)
    if not scanner or not qml_dir or not shutil.which("dpkg"):
        print("qmlimportscanner, the Qt QML directory or dpkg is unavailable",
              file=sys.stderr)
        return 1

    try:
        out = subprocess.run(
            [scanner, "-rootPath", root, "-importPath", qml_dir],
            capture_output=True, check=True).stdout
        entries = json.loads(out)
    except (subprocess.CalledProcessError, json.JSONDecodeError) as exc:
        print("qmlimportscanner failed: %s" % exc, file=sys.stderr)
        return 1

    names = set(IMPLICIT_MODULES)
    for entry in entries:
        name = entry.get("name")
        # Our own module is compiled into the binary, and a relative import is not a module.
        if not name or name == "Player" or entry.get("type") != "module":
            continue
        names.add(name)

    packages = set()
    unresolved = set()
    for name in sorted(names):
        qmldir = os.path.join(qml_dir, name.replace(".", "/"), "qmldir")
        if not os.path.exists(qmldir):
            # QtQml has no qmldir of its own: it lives inside libqt6qml6, which shlibdeps does
            # see, so there is nothing to add here.
            continue
        try:
            owner = subprocess.run(["dpkg", "-S", qmldir],
                                   capture_output=True, check=True, text=True).stdout
            packages.add(owner.split(":")[0].strip())
        except subprocess.CalledProcessError:
            unresolved.add(name)

    for name in sorted(unresolved):
        print("no package owns the qmldir for %s" % name, file=sys.stderr)

    if not packages:
        print("resolved no QML packages at all", file=sys.stderr)
        return 1

    for p in sorted(packages):
        print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
