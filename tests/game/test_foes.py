"""Step 5, the animals fight back (docs/plan.md): an animal within 4 m joins the fight and comes after your cat; close,
it attacks with its own animation, and when the attack lands your cat takes its damage, unless it's dodging (or out of
reach). One attacks at a time. Hit in its wind-up, it reels and its attack stops. Defeated prey goes on the Clan's
fresh-kill pile. Left far behind, it goes back to roaming; so do all of them when real-time combat goes off.

Run as test_sandbox_gate.py says."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402

PLACES = {"near": (0, 300)}
IDS = {}
DAMAGE = "(10 * (1 + 0.1 * (math.min(60, math.max(1, Game.cat():level())) - 1)))"   # rules.hpp's enemy_damage, a rabbit


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent, fighting_back=False)   # still while the scene's put out
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    IDS.clear()
    IDS.update(h.scene(agent, PLACES))
    agent.lua("Game.cat():heal() Game.cat():setStamina(100) return true")
    h.passive(agent, False)


def teardown(agent):
    h.passive(agent, True)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    agent.lua("Game.cat():heal() return true")
    h.restore_sandbox(agent)


def _foe(agent, name):
    return h.get(agent, f"foe {IDS[name]}")


def _until_foe(agent, name, state, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if _foe(agent, name) == state:
            return
        time.sleep(0.04)
    raise AssertionError(f"the {name} rabbit never got to {state!r} in {timeout} s (it's {_foe(agent, name)!r})")


def _health(agent):
    return agent.lua("return Game.cat():health()")


def _distance(agent, name):
    return agent.lua(f"""local a, me = WCMODS_SCENE["{name}"]:K2_GetActorLocation(), Game.cat():location()
        return math.sqrt((a.X - me.X)^2 + (a.Y - me.Y)^2)""")


def test_joins_and_comes_close(agent):
    """Within 4 m, it joins the fight and comes up to your cat."""
    h.until(agent, f'({h.MOD}.get("foes") or 0) >= 1', 2, "the rabbit to join the fight")
    assert _foe(agent, "near") in ("chase", "windup"), _foe(agent, "near")
    h.until(agent, f'(function() local a, me = WCMODS_SCENE.near:K2_GetActorLocation(), Game.cat():location() '
                   f'return math.sqrt((a.X - me.X)^2 + (a.Y - me.Y)^2) < 200 end)()', 6, "the rabbit to come close")


def test_its_attack_hurts(agent):
    """Its attack lands on your cat for the rules' damage, and your cat plays a hit reaction."""
    hits = h.get(agent, "cat_hits") or 0
    before = _health(agent)
    h.until(agent, f'({h.MOD}.get("cat_hits") or 0) > {hits}', 10, "the rabbit's attack to land on your cat")
    playing = agent.lua('local m = Game.get(Game.player(), "Mesh"):GetAnimInstance():GetCurrentActiveMontage() '
                        'return m and m:GetFName():ToString()')
    took = before - _health(agent)
    damage = agent.lua(f"return {DAMAGE}")
    assert abs(took - damage) < 1, f"your cat took {took:.1f}, not {damage:.1f}"
    assert playing and "Hit" in playing, playing


def test_a_dodge_avoids_it(agent):
    """A dodge back as its wind-up starts: 3 m away (or still in the dodge's safe moment) when it lands, your cat takes
    nothing. (Timing a dodge for the last moment of the wind-up from here is too tight: reading the game takes a
    while.)"""
    _until_foe(agent, "near", "windup", 10)
    h.action(agent, "dodge_back")
    before, hits = _health(agent), h.get(agent, "cat_hits") or 0
    _until_foe(agent, "near", "chase", 3)
    assert (h.get(agent, "cat_hits") or 0) == hits and _health(agent) == before, "the attack hurt your cat"


def test_one_attacks_at_a_time(agent):
    """With three fighting, never two attacking at once."""
    more = h.scene(agent, {"near": (0, 300), "left": (-35, 280), "right": (35, 280)})
    IDS.update(more)
    h.passive(agent, False)
    h.until(agent, f'({h.MOD}.get("foes") or 0) >= 3', 3, "all three to join")
    attacked = set()
    end = time.time() + 9
    while time.time() < end:
        states = agent.lua("local m = " + h.MOD + " return { near = m.get('foe " + str(IDS["near"]) + "'), "
                           "left = m.get('foe " + str(IDS["left"]) + "'), right = m.get('foe " + str(IDS["right"]) + "') }")
        busy = [n for n, s in states.items() if s in ("windup", "recover")]
        assert len(busy) <= 1, f"two attacking at once: {states}"
        attacked.update(busy)
        agent.lua("Game.cat():heal() return true")
        time.sleep(0.1)
    assert len(attacked) >= 2, f"only {attacked} attacked in 9 s"


def test_a_hit_in_its_windup_makes_it_reel(agent):
    """Your cat's light attack landing in its wind-up: it reels, and its attack doesn't land."""
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["near"])
    _until_foe(agent, "near", "windup", 10)
    hits = h.get(agent, "cat_hits") or 0
    h.action(agent, "light")             # lands 0.38 s in; its attack lands 0.75 s in
    _until_foe(agent, "near", "reel", 1.5)
    time.sleep(0.6)
    assert (h.get(agent, "cat_hits") or 0) == hits, "its attack landed though it reeled"


def test_defeat_feeds_the_clan(agent):
    """Beaten, a rabbit goes on the fresh-kill pile (5 more), and it's let go of."""
    agent.lua("Game.freshKill.set(40) return true")
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["near"])
    defeated = h.get(agent, "defeated") or 0
    for _ in range(10):
        agent.lua("Game.cat():setStamina(100) Game.cat():heal() return true")
        h.action(agent, "heavy")
        h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the attack to end")
        if (h.get(agent, "defeated") or 0) > defeated:
            break
        time.sleep(0.3)
    assert (h.get(agent, "defeated") or 0) > defeated, "the rabbit wasn't beaten in ten heavy attacks"
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
