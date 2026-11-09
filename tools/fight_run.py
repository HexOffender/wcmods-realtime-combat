"""A scripted player fights a live animal for a while, several ways, and everything the mod shares is logged ~8 times a
second, so the feel changes (#21) can be judged in numbers: how many strikes land, how many of the animal's attacks hit a
player who never dodges ("aggressive"), who attacks all the time and dodges a quarter second after the red mark ("dodger"),
who does the same but holds off attacking while the yellow or red mark shows ("careful"), or who only attacks in openings
and dodges at the mark ("patient").
Run at the save's own spot right after `agent launch --world` (the test cat's, Sandbox mode and this mod installed):
    python <wcmods>/wcmods.py agent run tools/fight_run.py [seconds-per-run] [style:kind,...]
e.g. `50 "dodger:fox,careful:fox"`; all six (three styles, a fox and a rabbit) by default. Writes fight_run.json where
the command runs; tools/analyze_fight.py reads it."""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "game"))
import helpers as h  # noqa: E402

OUT = os.path.abspath("fight_run.json")   # in the folder the command runs in

FOX = "Gameplay.Combat.Generator.Fixed.TitanFox.01.L13"   # the game's foxes are all "TitanFox" (rules.hpp: the fox profile)
RABBIT = h.RABBIT
SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 50
REACT = 0.25      # the player's reaction to the mark
OPENINGS = ("recover", "retreat", "reel", "pace", "approach")

# each loop (every share read costs a few ms: only what the player and the timeline need)
STATE = """
local m = WCModsCpp.mods["realtime-combat"]
local a = WCMODS_SCENE and WCMODS_SCENE.near
local me = Game.cat():location()
local d = -1
if a and Game.alive(a) then local p = a:K2_GetActorLocation() d = math.sqrt((p.X - me.X)^2 + (p.Y - me.Y)^2) end
local c = Game.cat()
return { foe = m.get("foe %(id)s") or "", attack = m.get("attack") or "", dodge = m.get("dodge") and true or false,
  hits = m.get("hits") or 0, attacks = m.get("attacks") or 0, cat_hits = m.get("cat_hits") or 0, mark = m.get("mark") or 0,
  warning = m.get("warning") or 0, dodges = m.get("dodges") or 0, dist = d, health = c:health(), stamina = c:stamina() }
"""
# before and after a run
TOTALS = """
local m = WCModsCpp.mods["realtime-combat"]
return { hits = m.get("hits") or 0, attacks = m.get("attacks") or 0, cat_hits = m.get("cat_hits") or 0, dodged = m.get("dodged") or 0,
  missed = m.get("missed") or 0, foe_attacks = m.get("foe_attacks") or 0, dodges = m.get("dodges") or 0, guards = m.get("guards") or 0,
  hops = m.get("hops") or 0, blocked = m.get("blocked_hits") or 0, feints = m.get("feints") or 0, combos = m.get("combos") or 0,
  counters = m.get("counters") or 0, refused = m.get("refused") or 0, dodge_refused = m.get("dodge_refused") or 0,
  fled = m.get("fled") or 0, gave_up = m.get("gave_up") or 0, defeated = m.get("defeated") or 0, foe = m.get("foe %(id)s") or "",
  hp = m.get("hp %(id)s") or -1 }
"""
COUNTS = ("hits", "attacks", "cat_hits", "dodged", "missed", "foe_attacks", "dodges", "guards", "hops", "blocked", "feints",
          "combos", "counters", "refused", "dodge_refused", "fled", "gave_up", "defeated")


def say(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def press(word):
    """An action through the test channel, with no wait after (helpers.action sleeps 0.15 s: too slow for a fight)."""
    agent.lua(f'WCModsCpp.share("RealtimeCombat.action", "{word} {next(h._count)}") return true')


def run(style, kind, seconds):
    h.passive(agent, True)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    # the view back to the first scene's yaw: the scene tries eighths from the camera's yaw, and after a fight (the lock-on
    # turned the camera) none of the eight it tried was level out to 7 m
    if YAW[0] is not None:
        h.set_view_yaw(agent, YAW[0])
        time.sleep(0.5)
    h.scene(agent, {"near": (0, 300), "beyond": (0, 700)}, spawn=False)
    YAW[0] = agent.lua("return WCMODS_SCENE_YAW")
    ids = h.scene(agent, {"near": (0, 300)}, kind=kind)
    id_ = ids["near"]
    agent.lua("Game.cat():heal() Game.cat():setStamina(100) return true")
    agent.lua("WCModsCpp.share('RealtimeCombat.foe_rolls', nil) WCModsCpp.share('RealtimeCombat.no_flee', true) return true")
    lua = STATE % {"id": id_}
    totals_lua = TOTALS % {"id": id_}
    h.action(agent, "lock")
    h.until_value(agent, "target", id_)
    h.passive(agent, False)
    first = agent.lua(totals_lua)
    rows, acts = [], []
    mark_at, press_at, heals, relocks = None, None, 0, 0
    t0 = time.time()
    end = t0 + seconds
    while time.time() < end:
        st = agent.lua(lua)
        now = time.time() - t0
        st["t"] = round(now, 3)
        rows.append(st)
        if st["health"] < MAX_HEALTH * 0.55:   # (a titan fox's blow can take 40: never let it fell your cat)
            agent.lua("Game.cat():heal() return true")
            heals += 1
            acts.append({"t": round(now, 3), "do": "heal"})
        if len(rows) % 10 == 0 and not h.get(agent, "locked") and st["foe"]:   # (a slower check: the lock let go)
            h.action(agent, "lock")
            relocks += 1
            acts.append({"t": round(now, 3), "do": "lock"})
        marked = int(st["mark"]) == id_
        if marked and mark_at is None:
            mark_at = now
            press_at = now + REACT   # seen: the press comes a reaction later, whether or not the mark's still up
        elif not marked:
            mark_at = None
        if style != "aggressive" and press_at is not None and now >= press_at and not st["dodge"]:
            press("dodge_back")
            acts.append({"t": round(now, 3), "do": "dodge", "mark_age": round(now - mark_at, 3) if mark_at else None,
                         "attack": st["attack"], "foe": st["foe"], "dist": round(st["dist"])})
            press_at = None
            continue
        if st["attack"] == "" and not st["dodge"] and st["stamina"] >= 12:
            warned = marked or int(st["warning"]) == id_   # the yellow or red mark: it's winding up
            if style == "patient":
                ok = st["foe"] in OPENINGS and not warned
            elif style == "careful":   # attacks whenever it can, but not while the mark shows (a player who reads it)
                ok = not warned
            else:
                ok = True
            if ok:
                press("light")
                acts.append({"t": round(now, 3), "do": "light", "foe": st["foe"], "dist": round(st["dist"])})
                continue
        time.sleep(0.02)
    last = agent.lua(totals_lua)
    totals = {k: last[k] - first[k] for k in COUNTS}
    totals.update({"heals": heals, "relocks": relocks, "samples": len(rows), "actions": len(acts), "foe_at_end": last["foe"],
                   "hp_at_end": last["hp"]})
    say(style, kind.split(".")[-3], json.dumps(totals))
    h.passive(agent, True)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    agent.lua("Game.cat():heal() Game.cat():setStamina(100) return true")
    time.sleep(1.0)
    return {"style": style, "kind": kind, "seconds": seconds, "totals": totals, "actions": acts, "rows": rows}


YAW = [None]
RUNS = [(s, k) for k in (FOX, RABBIT) for s in ("aggressive", "dodger", "patient")]
if len(sys.argv) > 2:   # e.g. "dodger:fox,patient:fox,aggressive:rabbit"
    RUNS = [(p.split(":")[0], FOX if p.split(":")[1] == "fox" else RABBIT) for p in sys.argv[2].split(",")]
h.turn_on(agent, fighting_back=True)
level = agent.lua("return Game.cat():level()")
MAX_HEALTH = agent.lua("return Game.cat():maxHealth()")
say("cat level", level, "runs of", SECONDS, "s:", RUNS)
results = []
if os.path.exists(OUT) and len(sys.argv) > 2:
    with open(OUT, encoding="utf-8") as f:
        results = json.load(f)["runs"]   # earlier runs kept
try:
    for style, kind in RUNS:
        if True:
            results.append(run(style, kind, SECONDS))
            with open(OUT, "w", encoding="utf-8") as f:
                json.dump({"level": level, "runs": results}, f)
finally:
    h.passive(agent, True)
    agent.lua("WCModsCpp.share('RealtimeCombat.foe_rolls', 0.99) return true")
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    agent.lua("Game.cat():heal() return true")
    h.restore_sandbox(agent)
say("DONE")
