"""Circling while locked on (docs/plan.md, step 2's follow-up): a cat can't walk sideways, so with a lock on, holding
right (D) walks your cat forward round the target, curving left as it goes, at about the distance it started; left (A)
goes round the other way. A rabbit is put out 2 m ahead, held still.

Run as test_sandbox_gate.py says."""
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402

IDS = {}


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    IDS.clear()
    # the rabbit 2 m ahead, and the ground level and clear round it (the circle's way)
    IDS.update(h.scene(agent, {"target": (0, 200), "l": (-40, 280), "r": (40, 280), "far": (0, 400)}))
    for extra in ("l", "r", "far"):   # only the rabbit ahead stays
        agent.lua(f'Game.animals.remove(WCMODS_SCENE["{extra}"]) WCMODS_SCENE["{extra}"] = nil return true')
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["target"])


def teardown(agent):
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    h.restore_sandbox(agent)


SAMPLE = """local a, me = WCMODS_SCENE.target:K2_GetActorLocation(), Game.cat():location()
    return { r = math.sqrt((me.X - a.X)^2 + (me.Y - a.Y)^2), around = math.deg(math.atan(me.Y - a.Y, me.X - a.X)),
             facing = Game.player():K2_GetActorRotation().Yaw, side = """ + h.MOD + """.get("circling") }"""


def _circle(agent, key, seconds=3.0):
    """Hold a key with the lock on; samples of your cat's distance from the rabbit, its angle round it, its facing."""
    from wcmods.agent import window
    hwnd, vk = window.game_window(), window.vk_of(key)
    samples = []
    window.post_key(hwnd, vk, True)
    try:
        end = time.time() + seconds
        while time.time() < end:
            samples.append(agent.lua(SAMPLE))
            time.sleep(0.15)
    finally:
        window.post_key(hwnd, vk, False)
    return samples


def _check(samples, way):
    """way +1: round to the right, -1 to the left. With the camera on the line from your cat to the rabbit, its right
    is clockwise round the rabbit seen from above (Unreal's yaw goes up turning right): your cat's angle round the rabbit
    goes down, and it faces 90 degrees less than that angle. The game turns a cat toward the way it's asked to go at
    about 80 degrees a second, so the first second is its swing onto the circle: the facing's checked after."""
    walking = [s for s in samples if s["side"] == way]
    assert len(walking) >= 5, f"circling was {[s['side'] for s in samples]}, not {way}"
    turned = sum(h.turn(a["around"], b["around"]) for a, b in zip(samples, samples[1:]))
    assert turned * way < 0, f"went round {turned:.0f} degrees, the wrong way"
    assert abs(turned) > 45, f"went only {turned:.0f} degrees round the rabbit"
    r0 = samples[0]["r"]
    rs = [s["r"] for s in walking]
    assert max(rs) < r0 * 1.4 + 40 and min(rs) > r0 * 0.6 - 40, f"the circle wasn't kept: {r0:.0f} at first, then {min(rs):.0f} to {max(rs):.0f}"
    # facing the way it walks: along the circle, not at the rabbit
    offs = [round(abs(h.turn(s["around"] - 90 * way, s["facing"]))) for s in walking[7:]]
    assert sorted(offs)[len(offs) // 2] < 40, f"your cat faced {offs} degrees off the circle's way"


def test_right_goes_round(agent):
    """D: forward round the rabbit to the right, facing the way it walks."""
    _check(_circle(agent, "D"), +1)


def test_left_goes_round_the_other_way(agent):
    """A: round the other way."""
    _check(_circle(agent, "A"), -1)
