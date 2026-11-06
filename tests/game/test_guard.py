"""Step 7, blocking and parrying (docs/plan.md): hold the block key and a hit from in front does a quarter of its damage,
the rest off stamina; with no stamina the guard breaks; a guard begun just before a hit lands parries it (no damage, the
animal reels, open to a riposte that does three times the damage). The right mouse button blocks. A rabbit fights your
cat, put out 3 m ahead.

The tests hold the block through a shared value (RealtimeCombat.block), and widen the parry's window (0.2 s) with
another (RealtimeCombat.parry_window): reading the game from here takes too long to hit 0.2 s. The real right mouse
button is tested too.

Run as test_sandbox_gate.py says."""
import ctypes
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402

IDS = {}
DAMAGE = "(10 * (1 + 0.1 * (math.min(60, math.max(1, Game.cat():level())) - 1)))"   # a rabbit's, rules.hpp's enemy_damage
LIGHT = "(8 + 1.1 * math.min(60, math.max(1, Game.cat():level())))"                  # your cat's light attack


def _block(agent, on):
    agent.lua(f'WCModsCpp.share("RealtimeCombat.block", {"true" if on else "false"}) return true')


def _parry_window(agent, seconds):
    agent.lua(f'WCModsCpp.share("RealtimeCombat.parry_window", {seconds}) return true')


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    _block(agent, False)
    _parry_window(agent, 0)
    IDS.clear()
    IDS.update(h.scene(agent, {"near": (0, 300)}))
    agent.lua("Game.cat():heal() Game.cat():setStamina(100) return true")
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["near"])


def teardown(agent):
    _block(agent, False)
    _parry_window(agent, 0)
    h.passive(agent, True)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    agent.lua("Game.cat():heal() return true")
    h.restore_sandbox(agent)


def _foe(agent):
    return h.get(agent, f"foe {IDS['near']}")


def _until_foe(agent, state, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if _foe(agent) == state:
            return
        time.sleep(0.03)
    raise AssertionError(f"the rabbit never got to {state!r} in {timeout} s (it's {_foe(agent)!r})")


def _cat(agent):
    return agent.lua("local c = Game.cat() return { c:health(), c:stamina() }")


def test_a_blocked_hit(agent):
    """Guarding from before its attack: a quarter of the damage, the rest off stamina."""
    _block(agent, True)
    h.until_value(agent, "guarding", True)
    h.passive(agent, False)
    blocks = h.get(agent, "blocks") or 0
    hp, st = _cat(agent)
    h.until(agent, f'({h.MOD}.get("blocks") or 0) > {blocks}', 12, "the rabbit's attack to land on the guard")
    hp2, st2 = _cat(agent)
    damage = agent.lua(f"return {DAMAGE}")
    assert abs((hp - hp2) - damage * 0.25) < 1, f"health {hp:.1f} to {hp2:.1f}: not a quarter of {damage:.1f}"
    assert abs((st - st2) - damage) < 2, f"stamina {st:.1f} to {st2:.1f}: not {damage:.1f} off"
    assert h.get(agent, "guarded") == "blocked" and h.get(agent, "guarding") is True


def test_the_guard_breaks_with_no_stamina(agent):
    """Guarding with no stamina: the guard breaks, your cat reels, and it takes the chip."""
    _block(agent, True)
    h.until_value(agent, "guarding", True)
    agent.lua("Game.cat():setStamina(0) return true")   # none comes back while it guards
    h.passive(agent, False)
    breaks = h.get(agent, "guard_breaks") or 0
    hp, _ = _cat(agent)
    h.until(agent, f'({h.MOD}.get("guard_breaks") or 0) > {breaks}', 12, "the guard to break")
    assert h.get(agent, "guarding") is False
    hp2, _ = _cat(agent)
    damage = agent.lua(f"return {DAMAGE}")
    assert abs((hp - hp2) - damage * 0.25) < 1, (hp, hp2, damage)


def test_a_parry_and_a_riposte(agent):
    """A guard raised as its attack winds up (the window widened for the test): parried, nothing taken, the rabbit
    reels; your cat's light attack then does three times its damage."""
    _parry_window(agent, 1.0)
    h.passive(agent, False)
    _until_foe(agent, "windup", 12)
    _block(agent, True)
    parries = h.get(agent, "parries") or 0
    hp, _ = _cat(agent)
    h.until(agent, f'({h.MOD}.get("parries") or 0) > {parries}', 2, "the parry")
    _block(agent, False)
    assert h.get(agent, "guarded") == "parried"
    assert _cat(agent)[0] == hp, "the parried hit hurt your cat"
    assert _foe(agent) == "reel"
    # the riposte: your cat's light attack, once the parry's played
    time.sleep(0.6)
    ripostes = h.get(agent, "ripostes") or 0
    before = h.get(agent, f"hp {IDS['near']}")
    h.action(agent, "light")
    h.until(agent, f'({h.MOD}.get("ripostes") or 0) > {ripostes}', 2, "the riposte to land")
    after = h.get(agent, f"hp {IDS['near']}")
    light = agent.lua(f"return {LIGHT}")
    assert abs((before - after) - min(before, light * 3)) < 1, f"the riposte did {before - after:.1f}, not three times {light:.1f}"


def test_an_attack_drops_the_guard(agent):
    """Attacking while guarding: the guard drops and the attack's made."""
    _block(agent, True)
    h.until_value(agent, "guarding", True)
    made = h.get(agent, "attacks") or 0
    h.action(agent, "light")
    h.until(agent, f'({h.MOD}.get("attacks") or 0) > {made}', 2, "the attack")
    assert h.get(agent, "guarding") is False
    _block(agent, False)


def test_right_mouse_blocks(agent):
    """The real right mouse button, held: your cat guards; let go, it doesn't. (The game also crouches on it; the guard
    pose covers that, your cat standing still.)"""
    from wcmods.agent import window
    hwnd = window.game_window()
    ctypes.windll.user32.PostMessageW(hwnd, 0x0204, 0x0002, (400 << 16) | 400)   # WM_RBUTTONDOWN
    try:
        h.until_value(agent, "guarding", True)
    finally:
        ctypes.windll.user32.PostMessageW(hwnd, 0x0205, 0, (400 << 16) | 400)    # WM_RBUTTONUP
    h.until_value(agent, "guarding", False)
