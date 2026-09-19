import os, re, sys, json, collections

root = "assets/presets"
rows = []
for dirpath, _, files in os.walk(root):
    for f in files:
        if not f.lower().endswith(".milk"):
            continue
        path = os.path.join(dirpath, f)
        try:
            text = open(path, "r", encoding="utf-8", errors="ignore").read()
        except Exception:
            continue
        low = text.lower()
        # Milkdrop 2 presets carry pixel shaders in warp_/comp_ sections.
        shader_lines = len(re.findall(r'^\s*(warp|comp)_\d+\s*=', text, re.M))
        has_shader = shader_lines > 0
        # Blur passes are the documented expensive feature: 73% of the corpus per the brief.
        uses_blur = bool(re.search(r'blur[123]', low))
        # Rough cost proxy: shader volume dominates, blur adds full-screen passes.
        cost = shader_lines + (60 if uses_blur else 0)
        rel = os.path.relpath(dirpath, root).split(os.sep)
        category = rel[0]
        # The pack's finer grouping, which the old selection threw away.
        style = os.sep.join(rel[1:])
        # "! Transition" holds transition effects, not standalone visuals.
        if category.startswith("!"):
            continue
        rows.append(dict(path=path, cost=cost, shader=has_shader, blur=uses_blur,
                         category=category, style=style))

n = len(rows)
md2 = sum(1 for r in rows if r["shader"])
blur = sum(1 for r in rows if r["blur"])
print(f"corpus: {n} presets")
print(f"  with pixel shaders (Milkdrop 2 style): {md2} ({100*md2/n:.0f}%)   brief says 84%")
print(f"  without (Milkdrop 1 style):            {n-md2} ({100*(n-md2)/n:.0f}%)   brief says 16%")
print(f"  using blur passes:                     {blur} ({100*blur/n:.0f}%)   brief says 73%")

# Curate by the pack's own sub-folders, not by category.
#
# The previous selection took the 48 cheapest presets from each of the ten categories. Measured
# against the taxonomy it ignored, that reached 47 of the pack's 184 sub-folders and missed 137
# visual styles outright, while piling 48 presets into Supernova/Radiate alone. 480 presets
# showing 47 kinds of thing.
#
# The sub-folder - Glowsticks, Nested Spiral, Rorschach, Polar Warp - is a human-curated
# description of what a preset looks like, and it is what someone browsing is actually choosing
# by. Taking a couple from each covers every style in fewer presets.
#
# Cost is kept, demoted to the tiebreaker inside each style, so the default still leans on the
# cheaper presets where there is a choice. It is only a proxy and is known to misclassify, so it
# decides which of two Rorschachs ships - never whether Rorschach ships at all.
# Presets that render essentially nothing on this machine, measured with PLAYER_PROBE over a
# six-second run with audio playing. They are excluded before selection rather than deleted
# afterwards, so the style they belong to still gets two working presets.
#
# Two separate faults, kept apart because they need different evidence:
#
#   * entirely dependent on an external texture - blank without it, fine with it. Reverie ships
#     no textures by design (see the README), so these can never work in a default install.
#   * blank whatever you do - supplying the texture they ask for changes nothing, so something
#     else is wrong with them. They fail the "should this be in the default set" test anyway.
#
# Verified individually by rendering each with and without a stand-in texture. Do not add to this
# list from reading a preset: the one that prompted the whole investigation, "Planet escher -
# cubesBW", looks texture-dependent and is black with the texture as well as without.
BLANK_PRESETS = {
    # texture-dependent: 0% non-black without, 100% with
    "Fractal/Core Tunnel/Tripgnosis - Wormhole.milk",
    # blank regardless of textures
    "Hypnotic/Illusion/amandio c, flexi, martin - Op illusions - curved1.milk",
    "Hypnotic/Illusion/amandio c, flexi, martin - Op illusions - poggendo.milk",
    "Supernova/Radiate/corsage digit.milk",
}

# Two per style: the cheapest, and the median.
#
# Taking the two cheapest was the first attempt and skewed the default badly - 45% with pixel
# shaders against the corpus's 84%, and a median cost proxy of zero, which means most of the
# default was Milkdrop 1 era presets with no shaders at all. Cheap, and visually plainer than
# the collection actually is. Since the visualiser is the reason this application exists, a
# default that under-sells it is the wrong trade.
#
# The cheapest guarantees every style has something that runs on a software renderer; the median
# is what that style actually looks like. 66% shaders, median cost 65.
by_style = collections.defaultdict(list)
excluded = 0
for r in rows:
    if os.path.relpath(r["path"], root) in BLANK_PRESETS:
        excluded += 1
        continue
    by_style[(r["category"], r["style"])].append(r)
if excluded:
    print(f"  excluded {excluded} preset(s) that render nothing")

picked = []
for key in sorted(by_style):
    items = sorted(by_style[key], key=lambda r: r["cost"])
    picked.append(items[0])
    if len(items) > 1:
        picked.append(items[len(items) // 2])
picked.sort(key=lambda r: (r["category"], r["style"], r["cost"]))

cats = sorted({r["category"] for r in rows})
styles_all = {(r["category"], r["style"]) for r in rows}
styles_hit = {(r["category"], r["style"]) for r in picked}

with open("assets/presets-curated.txt", "w", encoding="utf-8") as out:
    out.write("# Curated default preset set.\n")
    out.write("# Two per sub-folder, so every visual style the pack curates is represented:\n")
    out.write("# the cheapest by static cost proxy (pixel-shader volume + blur passes), which\n")
    out.write("# guarantees something that runs on a software renderer, and the median, which\n")
    out.write("# is what the style actually looks like. Regenerate with tools/curate-presets.py.\n")
    for r in picked:
        out.write(os.path.relpath(r["path"], root) + "\n")

pb = sum(1 for r in picked if r["blur"])
ps = sum(1 for r in picked if r["shader"])
print(f"\ncurated: {len(picked)} presets across {len(cats)} categories")
print(f"  styles covered: {len(styles_hit)} of {len(styles_all)}"
      f"  ({100*len(styles_hit)/len(styles_all):.0f}%)")
print(f"  with shaders: {100*ps/len(picked):.0f}%   using blur: {100*pb/len(picked):.0f}%")
median = sorted(r["cost"] for r in picked)[len(picked)//2]
allmed = sorted(r["cost"] for r in rows)[len(rows)//2]
print(f"  median cost proxy: {median}  (whole corpus: {allmed})")
