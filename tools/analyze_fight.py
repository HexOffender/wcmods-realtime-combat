"""What tools/fight_run.py's fight_run.json says (python tools/analyze_fight.py [fight_run.json]): per run, the strikes made and landed (and why the rest missed: the foe's state and distance as
the key went down), the foe's attacks and how many hit, were dodged or missed, and for each foe attack the timeline: wind-up
seen, the mark seen, the player's dodge, the blow."""
import json
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "fight_run.json"
with open(path, encoding="utf-8") as f:
    data = json.load(f)
print("cat level", data["level"])
for run in data["runs"]:
    rows, acts, tot = run["rows"], run["actions"], run["totals"]
    kind = run["kind"].split(".")[-3]
    print(f"\n=== {run['style']} vs {kind}, {run['seconds']} s, {tot['samples']} samples "
          f"({tot['samples'] / run['seconds']:.0f}/s)")
    print("   ", {k: v for k, v in tot.items() if k not in ("samples", "actions")})
    # each strike: did "hits" go up within 0.9 s of the press?
    presses = [a for a in acts if a["do"] == "light"]
    landed, missed, ignored = [], [], []
    for a in presses:
        before = next((r for r in rows if r["t"] >= a["t"]), None)
        if before is None:
            continue
        soon = [r for r in rows if a["t"] <= r["t"] <= a["t"] + 0.4]
        if not any(r["attacks"] > before["attacks"] for r in soon):
            ignored.append(a)   # the press made no attack: your cat reeling from a hit, or too little stamina
            continue
        after = max((r["hits"] for r in rows if a["t"] <= r["t"] <= a["t"] + 0.9), default=before["hits"])
        (landed if after > before["hits"] else missed).append(a)
    strikes = len(landed) + len(missed)
    print(f"    presses {len(presses)}: ignored {len(ignored)} (reeling or no stamina); strikes {strikes}: landed {len(landed)}, "
          f"missed {len(missed)}" + (f" ({100 * len(landed) / strikes:.0f}% land)" if strikes else ""))
    by = {}
    for a in ignored:
        by.setdefault(a["foe"], []).append(a["dist"])
    for foe, ds in sorted(by.items()):
        print(f"      ignored with the foe {foe!r}: {len(ds)}, at {min(ds)}-{max(ds)} cm")
    by = {}
    for a in missed:
        by.setdefault(a["foe"], []).append(a["dist"])
    for foe, ds in sorted(by.items()):
        print(f"      missed with the foe {foe!r}: {len(ds)}, at {min(ds)}-{max(ds)} cm (mean {sum(ds) / len(ds):.0f})")
    # each missed strike: the foe's state and distance as the blow came down (0.38 s after the press)
    for a in missed:
        near = [r for r in rows if a["t"] + 0.25 <= r["t"] <= a["t"] + 0.6]
        states = ">".join(dict.fromkeys(r["foe"] for r in near))
        dists = [round(r["dist"]) for r in near]
        print(f"        miss at t={a['t']:6.2f}: pressed with the foe {a['foe']!r} at {a['dist']} cm; "
              f"as it came down: {states or '?'} at {dists}")
    by = {}
    for a in landed:
        by.setdefault(a["foe"], []).append(a["dist"])
    for foe, ds in sorted(by.items()):
        print(f"      landed with the foe {foe!r}: {len(ds)}, at {min(ds)}-{max(ds)} cm (mean {sum(ds) / len(ds):.0f})")
    # each foe attack: from the wind-up's start to the blow
    windups = [rows[i]["t"] for i in range(1, len(rows)) if rows[i]["foe"] == "windup" and rows[i - 1]["foe"] != "windup"]
    if "foe_attacks" in rows[0]:
        starts = [rows[i]["t"] for i in range(1, len(rows)) if rows[i]["foe_attacks"] > rows[i - 1]["foe_attacks"]]
    else:
        starts = windups
    print(f"    foe attacks {tot['foe_attacks']} (wind-ups seen {len(windups)}); hit {tot['cat_hits']}, dodged {tot['dodged']}, "
          f"out of reach {tot['missed']}")
    dodges = [a for a in acts if a["do"] == "dodge"]
    for t in starts:
        window = [r for r in rows if t <= r["t"] <= t + 2.5]
        if not window:
            continue
        first = window[0]
        mark = next((r["t"] for r in window if int(r["mark"]) != 0), None)
        outcome, at = "?", None
        for r in window:
            for k, word in (("cat_hits", "HIT"), ("dodged", "dodged"), ("missed", "out of reach")):
                if k in r and r[k] > first[k]:
                    outcome, at = word, r["t"]
                    break
            if at is not None:
                break
        dodge = next((a for a in dodges if t <= a["t"] <= (at or t + 2.5)), None)
        states = []
        for r in window:
            if not states or states[-1] != r["foe"]:
                states.append(r["foe"])
        print(f"      t={t:6.2f} mark {'+%.2f' % (mark - t) if mark else '  -  '} "
              f"dodge {'+%.2f' % (dodge['t'] - t) if dodge else '  -  '}"
              f"{' (in ' + dodge['attack'] + ')' if dodge and dodge.get('attack') else ''} "
              f"blow {'+%.2f' % (at - t) if at else '  -  '} {outcome:12s} states {'>'.join(states[:6])}")
