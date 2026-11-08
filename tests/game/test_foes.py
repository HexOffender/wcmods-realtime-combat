"""The animals fight back (docs/plan.md, step 5, and the fight rework after it): an animal within 4 m joins the fight
and comes toward your cat, to its spacing, where it paces round it; it rushes in to attack with its own animation, and
when the attack lands your cat takes its damage, unless it's dodging (or out of reach). One attacks at a time. Hit in
its wind-up, it reels and its attack stops. Attacked while pacing it may guard (blocked hits do a fifth of their
damage and fill its posture; full, the guard breaks and it's open to a riposte) or hop aside, and it may feint. Prey
that's losing runs off. Defeated prey goes on the Clan's fresh-kill pile. Left far behind, it goes back to roaming; so
do all of them when real-time combat goes off.

The chances are fixed for each test (RealtimeCombat.foe_rolls): 0.99 comes up for none of them; a rabbit guards under
0.1, hops under 0.45 and feints under 0.15.

Run as test_sandbox_gate.py says."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402

PLACES = {"near": (0, 300)}
IDS = {}
DAMAGE = "(10 * (1 + 0.1 * (math.min(60, math.max(1, Game.cat():level())) - 1)))"   # rules.hpp's enemy_damage, a rabbit
LIGHT = "(8 + 1.1 * math.min(60, math.max(1, Game.cat():level())))"                  # rules.hpp's cat_damage, a light attack


def _rolls(agent, value):
    agent.lua("WCModsCpp.share('RealtimeCombat.foe_rolls', %s) return true" % ("nil" if value is None else value))


def _no_flee(agent, on):
    agent.lua("WCModsCpp.share('RealtimeCombat.no_flee', %s) return true" % ("true" if on else "nil"))


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent, fighting_back=False)   # still while the scene's put out
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    IDS.clear()
    # a fight needs level ground farther out than the rabbit's place: it paces, retreats and is pushed 3 to 7 m off,
    # and down the slope by the den it's out of sight (the lock lets it go): face a way that's level out to 7 m first
    h.scene(agent, {"near": (0, 300), "beyond": (0, 700)}, spawn=False)
    IDS.update(h.scene(agent, PLACES))
    agent.lua("Game.cat():heal() Game.cat():setStamina(100) return true")
    h.passive(agent, False)


def teardown(agent):
    h.passive(agent, True)
    _rolls(agent, 0.99)
    _no_flee(agent, True)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    agent.lua("Game.cat():heal() return true")
    h.restore_sandbox(agent)


def _foe(agent, name):
    return h.get(agent, f"foe {IDS[name]}")


def _until_foe(agent, name, states, timeout):
    states = (states,) if isinstance(states, str) else tuple(states)
    end = time.time() + timeout
    while time.time() < end:
        if _foe(agent, name) in states:
            return
        time.sleep(0.04)
    raise AssertionError(f"the {name} rabbit never got to {states} in {timeout} s (it's {_foe(agent, name)!r})")


def _health(agent):
    return agent.lua("return Game.cat():health()")


def _hp(agent, name):
    return h.get(agent, f"hp {IDS[name]}")


def _distance(agent, name):
    return agent.lua(f"""local a, me = WCMODS_SCENE["{name}"]:K2_GetActorLocation(), Game.cat():location()
        return math.sqrt((a.X - me.X)^2 + (a.Y - me.Y)^2)""")


def _lock(agent):
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["near"])
    assert h.get(agent, "target") == IDS["near"], \
        f"the lock didn't take the rabbit: locked {h.get(agent, 'locked')!r}, target {h.get(agent, 'target')!r}, " \
        f"rabbit {IDS['near']}, {_distance(agent, 'near'):.0f} cm off, its foe state {_foe(agent, 'near')!r}"


def _step_in(agent, name, to=150):
    """Your cat put `to` cm from it, facing it (a rabbit pacing 2.6 m off is out of a light attack's reach)."""
    agent.lua(f"""local a, me = WCMODS_SCENE["{name}"]:K2_GetActorLocation(), Game.cat():location()
        local d = math.sqrt((a.X - me.X)^2 + (a.Y - me.Y)^2)
        if d > {to} then
            local k = (d - {to}) / d
            Game.cat():teleport(me.X + (a.X - me.X) * k, me.Y + (a.Y - me.Y) * k, me.Z)
        end
        return true""")


def test_joins_and_keeps_its_distance(agent):
    """Within 4 m, it joins the fight, comes toward your cat and paces round it at its spacing, not in its face."""
    h.until(agent, f'({h.MOD}.get("foes") or 0) >= 1', 2, "the rabbit to join the fight")
    assert _foe(agent, "near") in ("approach", "pace", "windup"), _foe(agent, "near")
    _until_foe(agent, "near", "pace", 6)
    seen = []
    end = time.time() + 1.5
    while time.time() < end and _foe(agent, "near") == "pace":
        seen.append(_distance(agent, "near"))
        time.sleep(0.1)
    assert seen and min(seen) > 170 and max(seen) < 420, f"pacing at {min(seen):.0f} to {max(seen):.0f} cm"


def test_its_attack_hurts(agent):
    """It rushes in and its attack lands on your cat for the rules' damage, and your cat plays a hit reaction."""
    hits = h.get(agent, "cat_hits") or 0
    before = _health(agent)
    h.until(agent, f'({h.MOD}.get("cat_hits") or 0) > {hits}', 12, "the rabbit's attack to land on your cat")
    playing = agent.lua('local m = Game.get(Game.player(), "Mesh"):GetAnimInstance():GetCurrentActiveMontage() '
                        'return m and m:GetFName():ToString()')
    took = before - _health(agent)
    damage = agent.lua(f"return {DAMAGE}")
    assert abs(took - damage) < 1, f"your cat took {took:.1f}, not {damage:.1f}"
    assert playing and "Hit" in playing, playing


def test_it_backs_off_after_striking(agent):
    """After its strike it's open a moment, then backs off to its spacing and paces again."""
    _until_foe(agent, "near", "recover", 12)
    _until_foe(agent, "near", "retreat", 1.5)
    _until_foe(agent, "near", "pace", 2)
    assert _distance(agent, "near") > 200, f"it's {_distance(agent, 'near'):.0f} cm off after backing off"


def test_a_dodge_avoids_it(agent):
    """A dodge back a third of a second into its wind-up (about when the HUD's mark comes up): your cat's still in the
    dodge's safe moment (0.55 s) when the blow lands at 0.88 s, and takes nothing."""
    _until_foe(agent, "near", "windup", 12)
    time.sleep(0.3)
    h.action(agent, "dodge_back")
    before, hits = _health(agent), h.get(agent, "cat_hits") or 0
    _until_foe(agent, "near", ("retreat", "pace", "approach"), 4)
    assert (h.get(agent, "cat_hits") or 0) == hits and _health(agent) == before, "the attack hurt your cat"


def test_one_attacks_at_a_time(agent):
    """With three fighting, never two attacking at once."""
    more = h.scene(agent, {"near": (0, 300), "left": (-35, 280), "right": (35, 280)})
    IDS.update(more)
    h.passive(agent, False)
    h.until(agent, f'({h.MOD}.get("foes") or 0) >= 3', 3, "all three to join")
    attacked = set()
    end = time.time() + 14
    while time.time() < end:
        states = agent.lua("local m = " + h.MOD + " return { near = m.get('foe " + str(IDS["near"]) + "'), "
                           "left = m.get('foe " + str(IDS["left"]) + "'), right = m.get('foe " + str(IDS["right"]) + "') }")
        busy = [n for n, s in states.items() if s in ("windup", "recover")]
        assert len(busy) <= 1, f"two attacking at once: {states}"
        attacked.update(busy)
        agent.lua("Game.cat():heal() return true")
        time.sleep(0.1)
    assert len(attacked) >= 2, f"only {attacked} attacked in 14 s"


def test_a_hit_in_its_windup_makes_it_reel(agent):
    """Your cat's light attack landing in its wind-up: it reels, and its attack doesn't land. (It feints here, so its
    wind-up is long enough for the agent's round trips.)"""
    _rolls(agent, 0.0)
    _lock(agent)
    _until_foe(agent, "near", "windup", 12)
    _step_in(agent, "near", 140)
    hits = h.get(agent, "cat_hits") or 0
    h.action(agent, "light")             # lands 0.38 s in; its attack, held 0.3 s, lands 1.05 s in
    _until_foe(agent, "near", "reel", 1.5)
    time.sleep(0.6)
    assert (h.get(agent, "cat_hits") or 0) == hits, "its attack landed though it reeled"


def test_it_guards_and_its_guard_breaks(agent):
    """Attacked while it paces, it guards: a light hit from in front does a fifth of its damage and fills its posture;
    a heavy one straight after (its guard held up while it's attacked) fills it the rest of the way, the guard breaks
    and nothing gets through; your cat's next hit is a riposte (three times)."""
    _rolls(agent, 0.0)   # it guards (and feints when it attacks, and its cooldown's short)
    _lock(agent)
    _until_foe(agent, "near", "pace", 8)
    _step_in(agent, "near", 150)
    agent.lua("Game.cat():setStamina(100) return true")
    blocked, broken, hp = h.get(agent, "blocked_hits") or 0, h.get(agent, "broken_guards") or 0, _hp(agent, "near")
    ripostes = h.get(agent, "ripostes") or 0
    h.action(agent, "light")
    h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the light attack to end")
    h.action(agent, "heavy")             # at once, as a player chains them: its guard's still up
    try:
        h.until(agent, f'({h.MOD}.get("broken_guards") or 0) > {broken}', 2, "the heavy hit to break its guard")
    except AssertionError:
        raise AssertionError(f"no broken guard: it's {_foe(agent, 'near')!r}, {_distance(agent, 'near'):.0f} cm off; "
                             f"blocked {h.get(agent, 'blocked_hits')} (from {blocked}), posture {h.get(agent, 'target_posture')}")
    assert (h.get(agent, "blocked_hits") or 0) == blocked + 1, "the light hit wasn't blocked"
    light = agent.lua(f"return {LIGHT}")
    took = hp - _hp(agent, "near")
    assert abs(took - light * 0.2) < 0.6, f"it took {took:.1f}, not a fifth of a light hit ({light:.1f}) and nothing of the heavy"
    assert _foe(agent, "near") == "reel"
    h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the heavy attack to end")
    agent.lua("Game.cat():setStamina(100) return true")
    _step_in(agent, "near", 140)
    h.action(agent, "light")
    h.until(agent, f'({h.MOD}.get("ripostes") or 0) > {ripostes}', 2, "the riposte")


def test_it_hops_aside(agent):
    """Attacked while it paces, it hops aside: the light attack finds nothing."""
    _rolls(agent, 0.2)   # a hop (over its 0.1 guard chance, under 0.45), no feint
    _lock(agent)
    _until_foe(agent, "near", "pace", 8)
    _step_in(agent, "near", 150)
    hops, hp = h.get(agent, "hops") or 0, _hp(agent, "near")
    h.action(agent, "light")
    _until_foe(agent, "near", "hop", 1)
    h.until(agent, f'({h.MOD}.get("hops") or 0) > {hops}', 1, "the hop")
    h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the light attack to end")
    assert _hp(agent, "near") == hp, "the light attack hit it in its hop"


def test_a_feint_holds_its_strike(agent):
    """A feint: its wind-up is held a while, so its strike comes well after the usual 0.75 s."""
    _rolls(agent, 0.0)   # it feints (and holds as long as the rabbit's least, 0.3 s)
    feints = h.get(agent, "feints") or 0
    _until_foe(agent, "near", "windup", 12)
    started = time.time()
    _until_foe(agent, "near", "recover", 3)
    took = time.time() - started
    assert (h.get(agent, "feints") or 0) > feints, "no feint"
    assert took > 0.9, f"its strike came {took:.2f} s after its wind-up began"


def test_losing_prey_runs(agent):
    """A rabbit below a third of its health runs from your cat."""
    _no_flee(agent, False)
    _lock(agent)
    fled = h.get(agent, "fled") or 0
    for _ in range(12):
        if (h.get(agent, "fled") or 0) > fled:
            break
        agent.lua("Game.cat():setStamina(100) Game.cat():heal() return true")
        _step_in(agent, "near", 140)
        h.action(agent, "heavy")
        h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the attack to end")
        time.sleep(0.2)
    assert (h.get(agent, "fled") or 0) > fled, "it never ran"
    start = _distance(agent, "near")
    end = time.time() + 3
    while time.time() < end and _distance(agent, "near") <= start + 150:
        time.sleep(0.2)
    assert _distance(agent, "near") > start + 150, f"it isn't getting away ({start:.0f} to {_distance(agent, 'near'):.0f})"


def test_defeat_feeds_the_clan(agent):
    """Beaten, a rabbit goes on the fresh-kill pile (5 more), and it's let go of."""
    agent.lua("Game.freshKill.set(40) return true")
    _lock(agent)
    defeated = h.get(agent, "defeated") or 0
    tries = []   # each attack: the distance and its state before, then the hits and its health after
    for _ in range(14):
        agent.lua("Game.cat():setStamina(100) Game.cat():heal() return true")
        _step_in(agent, "near", 140)
        if h.get(agent, "target") != IDS["near"]:   # let go (out of sight two seconds, pushed down the slope): as a player would
            _lock(agent)
        before = (round(_distance(agent, "near")), _foe(agent, "near"), h.get(agent, "hits") or 0)
        h.action(agent, "heavy")
        h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the attack to end")
        tries.append(before + ((h.get(agent, "hits") or 0) - before[2], _hp(agent, "near")))
        if (h.get(agent, "defeated") or 0) > defeated:
            break
        time.sleep(0.3)
    assert (h.get(agent, "defeated") or 0) > defeated, \
        f"the rabbit wasn't beaten in fourteen heavy attacks (locked {h.get(agent, 'locked')!r} on {h.get(agent, 'target')!r}, " \
        f"rabbit {IDS['near']}; each try: cm off, state, hits before, hits landed, hp after: {tries})"
    pile = agent.lua("return Game.freshKill.value()")
    assert h.get(agent, "fresh_kill_given") == 5
    assert 43.5 <= pile <= 45.5, f"the fresh-kill pile is {pile}, not 45 (or a little less: it goes down on its own)"
    h.until(agent, f'{h.MOD}.get("foe {IDS["near"]}") == nil', 2, "the rabbit to be let go")


def test_it_gives_up(agent):
    """Your cat 30 m away: it comes after it, and led over 20 m from where it joined, it gives up and goes back to
    roaming."""
    h.until(agent, f'({h.MOD}.get("foes") or 0) >= 1', 2, "the rabbit to join the fight")
    gave_up = h.get(agent, "gave_up") or 0
    # 30 m off, some way with ground there (straight back from the test cat's spot is the hillside by the den)
    moved = agent.lua("""local me = Game.cat():location()
        for k = 0, 7 do
            local yaw = math.rad(WCMODS_SCENE_YAW + 180 + k * 45)
            local x, y = me.X + math.cos(yaw) * 3000, me.Y + math.sin(yaw) * 3000
            local g = Game.ground(x, y, me.Z + 1500)
            if g and Game.cat():teleport(x, y, g + 60) then
                local now = Game.cat():location()
                if math.sqrt((now.X - me.X)^2 + (now.Y - me.Y)^2) > 2500 then return true end
            end
        end
        return false""")
    assert moved, "couldn't put your cat 30 m off"
    h.until(agent, f'({h.MOD}.get("gave_up") or 0) > {gave_up}', 15, "the rabbit to give up")
    assert _foe(agent, "near") is None


def test_off_lets_them_go(agent):
    """Real-time combat going off: every animal fighting is let go."""
    h.until(agent, f'({h.MOD}.get("foes") or 0) >= 1', 2, "the rabbit to join the fight")
    h.sandbox(agent, False)
    h.until(agent, f'({h.MOD}.get("foes") or 0) == 0', 2, "the animals to be let go")
    h.sandbox(agent, True)
    h.until_value(agent, "state", "on")
