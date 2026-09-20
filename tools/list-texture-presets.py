#!/usr/bin/env python3
"""Regenerate assets/presets-textures.txt.

Lists every preset that names an external image, and which images it asks for, tab-separated.

Shipped rather than worked out at startup because it means reading every preset in the pack,
measured at 3.35s from a cold cache against the 115ms the whole index takes. Shipping the *names*
rather than just the paths is what lets the runtime stay current: it lists the texture directory
once and knows exactly which presets are satisfied, so dropping an image in clears the mark with
no rescan and nothing remembered between launches.

Run from the repository root.
"""
import os
import re

ROOT = "assets/presets"
OUT = "assets/presets-textures.txt"

# projectM's own built-in samplers. Milkdrop also allows sampling-mode prefixes - fw_, fc_, pw_,
# pc_ and their reversed spellings - which are not part of the filename. Counting those as
# external textures is the mistake that once put the built-in noise textures on a list; see the
# brief. A "randNN" sampler asks for a random image from the pool, which still needs a pool, so
# it counts.
BUILTIN = {"noise_hq", "noise_lq", "noise_lq_lite", "noise_mq", "noisevol_hq", "noisevol_lq",
           "main", "blur1", "blur2", "blur3", "fc_main", "pc_main"}
PREFIX = re.compile(r"^(fc_|fw_|pc_|pw_|cf_|cp_|wf_|wp_)+")
SAMPLER = re.compile(r"sampler_([A-Za-z0-9_]+)")


def main():
    rows = []
    for dirpath, _, filenames in os.walk(ROOT):
        for filename in filenames:
            if not filename.endswith(".milk"):
                continue
            rel = os.path.relpath(os.path.join(dirpath, filename), ROOT)
            # "!" categories are transitions and are excluded wherever a preset is offered.
            if rel.startswith("!"):
                continue
            with open(os.path.join(dirpath, filename), encoding="utf-8",
                      errors="replace") as handle:
                text = handle.read()
            names = []
            for name in SAMPLER.findall(text):
                stripped = PREFIX.sub("", name.lower())
                if stripped and stripped not in BUILTIN and stripped not in names:
                    names.append(stripped)
            if names:
                rows.append((rel, sorted(names)))
    rows.sort()

    comments = []
    if os.path.exists(OUT):
        with open(OUT, encoding="utf-8") as handle:
            comments = [ln.rstrip("\n") for ln in handle if ln.startswith("#")]
    with open(OUT, "w", encoding="utf-8") as out:
        if comments:
            out.write("\n".join(comments) + "\n")
        for rel, names in rows:
            out.write(f"{rel}\t{','.join(names)}\n")
    print(f"  {len(rows)} preset(s) use an external image "
          f"({sum(len(n) for _, n in rows)} references) -> {OUT}")


if __name__ == "__main__":
    main()
