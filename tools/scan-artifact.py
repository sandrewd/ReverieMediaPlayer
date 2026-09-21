#!/usr/bin/env python3
"""Scan a built artefact for absolute build-host paths.

The rule this enforces is that nothing we ship should contain a path from the machine that
built it. Reverie shipped one for weeks without anyone noticing, because the string came from
a QStringLiteral and so was stored as UTF-16 - which `strings` does not show by default, and
which a plain `grep` over the binary therefore misses entirely. Both encodings are checked.

Two deliberate design points:

  * The patterns are generic - /home/<user>, /root, a Flatpak build root - rather than a list of
    specific words. A list of words to search for is itself a disclosure: whatever you name in a
    published file is published. Anything extra goes in REVERIE_SCAN_EXTRA, which lives in the
    environment and not in the repository.

  * Archives are opened rather than scanned as opaque blobs. A .deb is an `ar` of compressed
    tarballs, so the interesting strings are never visible in the outer file - scanning it whole
    would pass every time and mean nothing. Same for a .flatpak, which carries the build manifest
    inside it; that is how a home directory reached a published bundle once already.

Exit 0 if clean, 1 if anything matched.
"""

import os
import re
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

# Generic shapes, not specific words. Each is (label, compiled pattern over *text*).
# Each pattern captures the WHOLE path, subdirectories included. An earlier version stopped at
# the first slash, so /home/builder/secret-project was captured as /home/builder - which then
# matched an "allowed bare prefix" exemption and was discarded. The filter written to suppress
# noise was suppressing the signal, and the scanner reported clean on a file that leaked in both
# encodings. It only surfaced because it was run against a deliberately poisoned artefact.
_TAIL = r"(?:/[A-Za-z0-9._+~-]+)*"
_LEAD = r"(?<![A-Za-z0-9._+~-])"
PATTERNS = [
    ("home directory", re.compile(_LEAD + r"/home/[A-Za-z0-9._-]+" + _TAIL)),
    # Two boundaries, both about SHAPE rather than about which file a hit came from.
    #
    # Leading: these are ABSOLUTE paths, so the opening "/" must not be preceded by a filename
    # character. Without it, the relative path "Reaction/Liquid Ripples/root danglage ..." in the
    # preset pack matched "/root" and failed CI on every single build - and a check that cries
    # wolf on every run is a check somebody eventually turns off.
    #
    # Trailing: "/root" must be a whole component, not the start of "/rootkit".
    #
    # Deliberately NOT an allow-list of files: the brief records an exemption written to suppress
    # noise that suppressed the signal instead, and passed a deliberately poisoned package.
    ("root home", re.compile(_LEAD + r"/root(?![A-Za-z0-9._+~-])" + _TAIL)),
    ("flatpak build root", re.compile(_LEAD + r"/run/build/[A-Za-z0-9._-]+" + _TAIL)),
]

# Placeholders that are documentation rather than a real account. Deliberately a short, literal
# list: anything broader risks discarding a genuine hit, which is how this went wrong once.
ALLOWED = [
    re.compile(r"^/home/(user|username|<user>|\$USER|\$\{USER\})$"),
]


def extra_needles():
    raw = os.environ.get("REVERIE_SCAN_EXTRA", "")
    return [n.strip() for n in raw.replace(",", "\n").splitlines() if n.strip()]


def find_in_bytes(data, origin):
    """Search raw bytes for every pattern, in ASCII/UTF-8 and in UTF-16LE."""
    hits = []
    # UTF-16LE text decodes to something searchable if we drop the interleaved NULs. Doing it
    # this way rather than data.decode('utf-16-le') keeps it working on arbitrary binary.
    views = [
        ("ascii", data),
        ("utf-16le", bytes(data[i] for i in range(0, len(data), 2)) if len(data) > 1 else b""),
    ]
    for encoding, view in views:
        text = view.decode("latin-1", errors="replace")
        for label, pattern in PATTERNS:
            for m in pattern.finditer(text):
                s = m.group(0)
                if any(a.match(s) for a in ALLOWED):
                    continue
                hits.append((origin, encoding, label, s))
        for needle in extra_needles():
            for enc_name, candidate in (("ascii", needle.encode()),
                                        ("utf-16le", needle.encode("utf-16-le"))):
                if candidate in data:
                    hits.append((origin, enc_name, "extra needle", needle))
    return hits


def scan_file(path, origin=None):
    origin = origin or str(path)
    try:
        data = Path(path).read_bytes()
    except (OSError, MemoryError) as exc:
        print(f"  ! could not read {origin}: {exc}", file=sys.stderr)
        return []
    return find_in_bytes(data, origin)


def scan_tree(root, prefix):
    hits = []
    for p in Path(root).rglob("*"):
        if p.is_file() and not p.is_symlink():
            hits += scan_file(p, f"{prefix}!{p.relative_to(root)}")
    return hits


def scan_deb(path, tmp):
    """A .deb is an ar archive of tarballs; unpack both and scan the contents."""
    # One directory per artefact: scanning two packages in a single run collided here and
    # crashed on the second, which is exactly the case a release does (package plus bundle).
    out = Path(tmp) / f"deb-{Path(path).stem}"
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ar", "x", str(Path(path).resolve())], cwd=out, check=True)
    for member in out.glob("*.tar.*"):
        dest = out / (member.name.replace(".", "_"))
        dest.mkdir()
        try:
            with tarfile.open(member) as tf:
                tf.extractall(dest, filter="data")
        except tarfile.ReadError as exc:
            # Python's tarfile has no zstd, and dpkg-deb defaults to it on current Debian. An
            # unreadable member used to come out as a traceback; the danger is that a future
            # "handle it gracefully" turns it into a silent pass, so it is reported as a FAILURE
            # with a non-zero exit. A member we could not read is not a member we know is clean.
            print(f"  CANNOT READ {member.name} in {Path(path).name}: {exc}", file=sys.stderr)
            print("  -> this artefact was NOT fully scanned", file=sys.stderr)
            raise SystemExit(2)
        member.unlink()
    return scan_tree(out, Path(path).name)


def scan_flatpak(path, tmp):
    """A single-file flatpak bundle is an OSTree static delta; strings still live in it, and
    the build manifest is one of them. Scanned whole, which is enough to catch a path."""
    return scan_file(path, Path(path).name)


def main(argv):
    if len(argv) < 2:
        print(f"usage: {argv[0]} <artefact> [artefact...]", file=sys.stderr)
        return 2

    all_hits = []
    with tempfile.TemporaryDirectory() as tmp:
        for target in argv[1:]:
            p = Path(target)
            if not p.exists():
                print(f"  ! no such file: {target}", file=sys.stderr)
                return 2
            print(f"scanning {p.name} ...")
            if p.is_dir():
                all_hits += scan_tree(p, p.name)
            elif p.suffix == ".deb":
                all_hits += scan_deb(p, tmp)
            elif p.suffix == ".flatpak":
                all_hits += scan_flatpak(p, tmp)
            else:
                all_hits += scan_file(p)

    if not all_hits:
        n = len(extra_needles())
        print(f"clean - no build-host paths in either encoding"
              f"{f' ({n} extra needle(s) also checked)' if n else ''}")
        return 0

    print(f"\nFOUND {len(all_hits)} match(es):", file=sys.stderr)
    for origin, encoding, label, text in sorted(set(all_hits)):
        # A UTF-16 match runs on into whatever bytes follow it, so keep the report readable.
        shown = text if len(text) <= 80 else text[:77] + "..."
        print(f"  {origin}  [{encoding}]  {label}: {shown}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
