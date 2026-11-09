"""Step 4, the dodge (docs/plan.md): Left Alt leaps your cat about 1.5 m the way the player's moving, or straight back
when they aren't: not locked on, a jump that way, your cat turned to face it; locked on, your cat faces its target and
the game's side hop plays for a dodge to a side of it. Nothing hurts your cat for its first 0.55 s; it costs 15 stamina;
a wall (here a rabbit) stops it short; it can cut an attack short once the attack has landed, and pressed before that
it waits for the landing.

Run as test_sandbox_gate.py says."""
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    # the camera and your cat turned to where the ground's level and clear 3.4 m every way, no animals near
    h.scene(agent, {"f": (0, 340), "r": (90, 340), "b": (180, 340), "l": (-90, 340)}, spawn=False)
    agent.lua("Game.cat():setStamina(100) return true")


def teardown(agent):
    h.until(agent, f'not {h.MOD}.get("dodge") and ({h.MOD}.get("attack") or "") == ""', 5, "the dodge to end")
    h.clear_scene(agent)
    h.restore_sandbox(agent)


def _at(agent):
    return agent.lua("local p = Game.cat():location() return { p.X, p.Y }")


def _dodge(agent, word):
    """A dodge through the test channel; where your cat went (distance, and yaw)."""
    start = _at(agent)
    before = h.get(agent, "dodges") or 0
    h.action(agent, word)
    h.until(agent, f'({h.MOD}.get("dodges") or 0) > {before}', 2, "the dodge to start")
    h.until(agent, f'not {h.MOD}.get("dodge")', 3, "the dodge to end")
    end = _at(agent)
    dx, dy = end[0] - start[0], end[1] - start[1]
    return math.hypot(dx, dy), math.degrees(math.atan2(dy, dx))


def _montage(agent):
    """The montage playing on your cat, or None. (With nothing playing, GetCurrentActiveMontage gives an object that
    isn't valid, and calling anything on it crashes the game: IsValid first.)"""
    return agent.lua('local m = Game.get(Game.player(), "Mesh"):GetAnimInstance():GetCurrentActiveMontage() '
                     'if m and m:IsValid() then return m:GetFName():ToString() end return nil')


def test_each_way(agent):
    """Right, left, forward and back of the camera, nothing locked on: about 1.5 m each (the settings' dodge_distance_m),
    the right way, as a jump (no animation of its own) with your cat turned to face the way it goes."""
    view = h.control_yaw(agent)
    for word, way in (("dodge_right", 90), ("dodge_left", -90), ("dodge_forward", 0), ("dodge_back", 180)):
        agent.lua("Game.cat():setStamina(100) return true")
        before = h.get(agent, "dodges") or 0
        start = _at(agent)
        h.action(agent, word)
        h.until(agent, f'({h.MOD}.get("dodges") or 0) > {before}', 2, "the dodge to start")
        playing = _montage(agent)
        facing = agent.lua("return Game.player():K2_GetActorRotation().Yaw")
        h.until(agent, f'not {h.MOD}.get("dodge")', 3, "the dodge to end")
        end = _at(agent)
        moved = math.hypot(end[0] - start[0], end[1] - start[1])
        went = math.degrees(math.atan2(end[1] - start[1], end[0] - start[0]))
        assert 90 < moved < 230, f"{word}: moved {moved:.0f} units"
        off = abs(h.turn(view + way, went))
        assert off < 25, f"{word}: went {off:.0f} degrees off the way asked"
        assert playing is None or "Dodge" not in playing, f"{word}: the side hop {playing} played for a jump"
        assert abs(h.turn(view + way, facing)) < 25, f"{word}: your cat faced {h.turn(view + way, facing):.0f} degrees off the way it went"
        time.sleep(0.7)   # the stamina pause


def test_locked_on_a_side_hop_faces_the_target(agent):
    """Locked on to a rabbit 3 m ahead, a dodge to the right is the game's right hop, your cat facing the rabbit as it
    goes, about 1.5 m to the camera's right; a dodge back is a jump away from it, still facing it."""
    h.scene(agent, {"ahead": (0, 300)})
    agent.lua("Game.cat():setStamina(100) return true")
    h.action(agent, "lock")
    h.until_value(agent, "locked", True)
    view = h.control_yaw(agent)
    try:
        for word, way, hop in (("dodge_right", 90, "AM_Dodge_R"), ("dodge_back", 180, None)):
            agent.lua("Game.cat():setStamina(100) return true")
            before = h.get(agent, "dodges") or 0
            start = _at(agent)
            to_rabbit = h.yaw_to(agent, "ahead")   # as the dodge begins (a hop to the side moves the rabbit's bearing)
            h.action(agent, word)
            h.until(agent, f'({h.MOD}.get("dodges") or 0) > {before}', 2, "the dodge to start")
            playing = _montage(agent)
            facing = agent.lua("return Game.player():K2_GetActorRotation().Yaw")
            h.until(agent, f'not {h.MOD}.get("dodge")', 3, "the dodge to end")
            end = _at(agent)
            moved = math.hypot(end[0] - start[0], end[1] - start[1])
            went = math.degrees(math.atan2(end[1] - start[1], end[0] - start[0]))
            assert 90 < moved < 230, f"{word}: moved {moved:.0f} units"
            assert abs(h.turn(view + way, went)) < 30, f"{word}: went {h.turn(view + way, went):.0f} degrees off the way asked"
            assert abs(h.turn(to_rabbit, facing)) < 25, f"{word}: your cat faced {h.turn(to_rabbit, facing):.0f} degrees off the rabbit"
            if hop:
                assert playing == hop, f"{word}: {playing} played, not the {hop} hop"
            else:
                assert playing is None or "Dodge" not in playing, f"{word}: the side hop {playing} played for a jump"
            time.sleep(0.7)
    finally:
        h.action(agent, "lock")
        h.clear_scene(agent)


def test_standing_still_goes_back(agent):
    """With no movement keys held, the key hops your cat straight back from where it faces."""
    facing = agent.lua("return Game.player():K2_GetActorRotation().Yaw")
    moved, went = _dodge(agent, "dodge")
    assert moved > 90, moved
    assert abs(h.turn(facing + 180, went)) < 25, (facing, went)


def test_safe_then_not_and_stamina(agent):
    """Nothing hurts your cat at its start; after 0.55 s it can be hurt again. It costs 15 stamina."""
    before, safe = h.get(agent, "dodges") or 0, h.get(agent, "dodge_safe_starts") or 0
    h.action(agent, "dodge_right")
    h.until(agent, f'({h.MOD}.get("dodges") or 0) > {before}', 2, "the dodge to start")
    stamina = agent.lua("return Game.cat():stamina()")
    # it was safe as it began (counted: 0.55 s is too short to be sure of catching it from here), and isn't now
    assert (h.get(agent, "dodge_safe_starts") or 0) == safe + 1
    assert 83 <= stamina <= 86, stamina
    time.sleep(0.7)
    assert h.get(agent, "dodge_safe") is False


def test_a_wall_stops_it(agent):
    """An animal in a fight with your cat (locked on to) 1.5 m to the right stops the hop short of it."""
    h.scene(agent, {"wall": (90, 150)})
    h.set_view_yaw(agent, agent.lua("return WCMODS_SCENE_YAW") + 90)
    time.sleep(0.3)
    h.action(agent, "lock")
    h.until_value(agent, "locked", True)
    h.until(agent, f'({h.MOD}.get("blocking") or 0) >= 1', 2, "its body to block")
    moved, _ = _dodge(agent, "dodge_forward")   # the camera on it: forward is toward it
    h.action(agent, "lock")
    assert moved < 130, f"your cat went {moved:.0f} units, through what was in the way"


def test_too_little_stamina(agent):
    """With no stamina the dodge is refused. (The stamina's set to none and the dodge asked in one call: it comes back
    at 20 a second, and a few round trips later there'd be enough for half the cost.)"""
    refused, made = h.get(agent, "dodge_refused") or 0, h.get(agent, "dodges") or 0
    agent.lua(f'Game.cat():setStamina(0) WCModsCpp.share("RealtimeCombat.action", "dodge_right {next(h._count)}") return true')
    time.sleep(0.3)
    assert (h.get(agent, "dodge_refused") or 0) == refused + 1 and (h.get(agent, "dodges") or 0) == made, \
        (h.get(agent, "dodge_refused"), refused, h.get(agent, "dodges"), made)
    agent.lua("Game.cat():setStamina(100) return true")


def test_out_of_an_attack(agent):
    """A dodge pressed in an attack's wind-up waits, and goes the moment the attack lands (cutting the rest of it
    short); pressed once it's landed, the dodge cuts it short at once."""
    made, kept = h.get(agent, "dodges") or 0, h.get(agent, "dodges_kept") or 0
    agent.lua(f'WCModsCpp.share("RealtimeCombat.action", "light {next(h._count)}") return true')
    time.sleep(0.12)                          # the wind-up (it lands at 0.38 s)
    agent.lua(f'WCModsCpp.share("RealtimeCombat.action", "dodge_right {next(h._count)}") return true')
    got = agent.lua(f'return {{ {h.MOD}.get("dodges") or 0, {h.MOD}.get("attack") or "" }}')
    assert got[0] == made and got[1] == "light", f"the dodge went in the wind-up ({got})"
    h.until(agent, f'({h.MOD}.get("dodges") or 0) > {made}', 1, "the kept dodge to go as the attack lands")
    assert (h.get(agent, "dodges_kept") or 0) == kept + 1, "the dodge wasn't the kept one"
    assert h.get(agent, "attack") == "", "the attack went on after the dodge"
    h.until(agent, f'not {h.MOD}.get("dodge")', 3, "the dodge to end")
    time.sleep(0.7)
    agent.lua("Game.cat():setStamina(100) return true")
    made = h.get(agent, "dodges") or 0
    h.action(agent, "light")
    time.sleep(0.4)                          # landed at 0.38 s
    h.action(agent, "dodge_right")
    h.until(agent, f'({h.MOD}.get("dodges") or 0) > {made}', 2, "the dodge to cut the attack short")
    assert h.get(agent, "attack") == ""
    assert (h.get(agent, "dodges_kept") or 0) == kept + 1, "a dodge after the landing was counted as kept"


def test_the_real_key(agent):
    """Left Alt with D held: a dodge to the right, through the game's own input."""
    view = h.control_yaw(agent)
    start = _at(agent)
    before = h.get(agent, "dodges") or 0
    from wcmods.agent import window
    hwnd = window.game_window()
    d, alt = window.vk_of("D"), window.vk_of("LeftAlt")
    window.post_key(hwnd, d, True)
    try:
        time.sleep(0.25)
        window.post_key(hwnd, alt, True)
        time.sleep(0.08)
        window.post_key(hwnd, alt, False)
        h.until(agent, f'({h.MOD}.get("dodges") or 0) > {before}', 2, "Left Alt to dodge")
        h.until(agent, f'not {h.MOD}.get("dodge")', 3, "the dodge to end")
    finally:
        window.post_key(hwnd, d, False)
    way = h.get(agent, "dodge_way")
    assert abs(h.turn(view + 90, way)) < 30, f"the dodge went {h.turn(view, way):.0f} degrees right of the camera, not 90"
