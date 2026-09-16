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
        category = os.path.relpath(dirpath, root).split(os.sep)[0]
        # "! Transition" holds transition effects, not standalone visuals.
        if category.startswith("!"):
            continue
        rows.append(dict(path=path, cost=cost, shader=has_shader, blur=uses_blur,
                         category=category))

n = len(rows)
md2 = sum(1 for r in rows if r["shader"])
blur = sum(1 for r in rows if r["blur"])
print(f"corpus: {n} presets")
print(f"  with pixel shaders (Milkdrop 2 style): {md2} ({100*md2/n:.0f}%)   brief says 84%")
print(f"  without (Milkdrop 1 style):            {n-md2} ({100*(n-md2)/n:.0f}%)   brief says 16%")
print(f"  using blur passes:                     {blur} ({100*blur/n:.0f}%)   brief says 73%")

# Curate: cheapest presets, but spread across categories so the default set is not all
# one visual style. A teenager does not want to browse 9,795 presets.
TARGET = 480
by_cat = collections.defaultdict(list)
for r in rows:
    by_cat[r["category"]].append(r)
cats = sorted(by_cat)
per_cat = max(3, TARGET // max(1, len(cats)))
picked = []
for c in cats:
    items = sorted(by_cat[c], key=lambda r: r["cost"])
    picked.extend(items[:per_cat])
picked.sort(key=lambda r: r["cost"])
picked = picked[:TARGET]

with open("assets/presets-curated.txt", "w", encoding="utf-8") as out:
    out.write("# Curated default preset set.\n")
    out.write("# Selected by static cost proxy (pixel-shader volume + blur passes), spread\n")
    out.write("# across categories for variety. Regenerate with tools/curate-presets.py.\n")
    for r in picked:
        out.write(os.path.relpath(r["path"], root) + "\n")

pb = sum(1 for r in picked if r["blur"])
ps = sum(1 for r in picked if r["shader"])
print(f"\ncurated: {len(picked)} presets across {len(cats)} categories")
print(f"  with shaders: {100*ps/len(picked):.0f}%   using blur: {100*pb/len(picked):.0f}%")
