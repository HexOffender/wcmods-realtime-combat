"""Step 2, lock-on (docs/plan.md): the lock key picks the animal nearest the middle of the view, the camera turns to it
and follows it, the mouse wheel moves to the next one to a side, a marker sits on it, and it lets go on the key, when
the animal's gone or too far, and when real-time combat goes off. Two rabbits are put out each time, held still: one
straight ahead 700 units off, one nearer (500) but 40 degrees to the right.

Run as test_sandbox_gate.py says."""
import ctypes
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402

PLACES = {"ahead": (0, 700), "right": (40, 500)}
IDS = {}


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    IDS.clear()
    IDS.update(h.scene(agent, PLACES))


def teardown(agent):
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    h.restore_sandbox(agent)


def test_picks_the_one_in_the_middle(agent):
    """The lock key: the rabbit straight ahead, not the nearer one off to the side; its marker shows."""
    view = h.turn(agent.lua("return WCMODS_SCENE_YAW"), h.control_yaw(agent))
    h.action(agent, "lock")
    h.until_value(agent, "locked", True)
    picked = h.get(agent, "target")
    assert picked == IDS["ahead"], (f"picked {'the right one' if picked == IDS['right'] else picked}, "
                                    f"with the view {view:.1f} degrees right of where the scene was put; "
                                    f"from {h.get(agent, 'last_pick')} (ahead {IDS['ahead']}, right {IDS['right']})")
    assert h.get(agent, "target_model") == "SK_Rabbit"
    try:
        h.until_value(agent, "marker", True)
    except AssertionError as e:
        raise AssertionError(f"{e}; the HUD: {h.get(agent, 'marker_hud')}") from None
    # where the mod put it, against the engine's own projection of the rabbit (when that answers), in the UI's units
    time.sleep(0.5)   # the camera settled on it
    engine = agent.lua("""local a = WCMODS_SCENE.ahead:K2_GetActorLocation()
        local gs = StaticFindObject('/Script/Engine.Default__GameplayStatics') local pos = {}
        local ok = gs:ProjectWorldToScreen(Game.controller(), {X = a.X, Y = a.Y, Z = a.Z}, pos, false)
        local scale = StaticFindObject('/Script/UMG.Default__WidgetLayoutLibrary'):GetViewportScale(Game.controller())
        return ok and { pos.X / scale, pos.Y / scale } or false""")
    if engine:
        mine = (h.get(agent, "marker_x"), h.get(agent, "marker_y"))
        assert abs(mine[0] - engine[0]) < 60 and abs(mine[1] - engine[1]) < 60, (mine, engine)
    agent.shot("lock-on-marker")


def test_the_camera_turns_to_it_and_follows(agent):
    """Locked on to the one to the right, the camera turns to it within a second or so; moved round, it follows."""
    h.action(agent, "lock")
    h.until_value(agent, "locked", True)
    h.action(agent, "next_right")
    h.until_value(agent, "target", IDS["right"])
    time.sleep(1.5)
    off = abs(h.turn(h.control_yaw(agent), h.yaw_to(agent, "right")))
    assert off < 5, f"the camera is {off:.1f} degrees off the target"
    # your cat moved 300 units to the side: the camera follows
    agent.lua("""local me, yaw = Game.cat():location(), math.rad(WCMODS_SCENE_YAW - 90)
        Game.cat():teleport(me.X + math.cos(yaw) * 300, me.Y + math.sin(yaw) * 300, me.Z + 20) return true""")
    time.sleep(1.5)
    off = abs(h.turn(h.control_yaw(agent), h.yaw_to(agent, "right")))
    assert off < 5, f"after your cat moved, the camera is {off:.1f} degrees off the target"


def test_next_target_to_each_side(agent):
    """The wheel: right moves to the one to the right, left back; with none further that way, it stays."""
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["ahead"])
    h.action(agent, "next_right")
    h.until_value(agent, "target", IDS["right"])
    h.action(agent, "next_right")
    time.sleep(0.3)
    assert h.get(agent, "target") == IDS["right"], "it moved on with nothing further right"
    h.action(agent, "next_left")
    h.until_value(agent, "target", IDS["ahead"])


def test_lets_go(agent):
    """The key again lets go; so does the animal going (taken out of the world), and your cat going too far."""
    h.action(agent, "lock")
    h.until_value(agent, "locked", True)
    h.action(agent, "lock")
    h.until_value(agent, "locked", False)
    assert h.get(agent, "released") == "key" and h.get(agent, "marker") is False
    # gone
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["ahead"])
    agent.lua('Game.animals.remove(WCMODS_SCENE["ahead"]) WCMODS_SCENE["ahead"] = nil return true')
    h.until_value(agent, "locked", False)
    assert h.get(agent, "released") == "gone"
    # too far: your cat 2,500 units back
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["right"])
    agent.lua("""local me, yaw = Game.cat():location(), math.rad(WCMODS_SCENE_YAW + 180)
        Game.cat():teleport(me.X + math.cos(yaw) * 2500, me.Y + math.sin(yaw) * 2500, me.Z + 40) return true""")
    h.until_value(agent, "locked", False)
    assert h.get(agent, "released") == "too-far"


def test_off_lets_go(agent):
    """Real-time combat going off (not a sandbox cat any more) lets go of the lock."""
    h.action(agent, "lock")
    h.until_value(agent, "locked", True)
    h.sandbox(agent, False)
    h.until_value(agent, "locked", False)
    assert h.get(agent, "released") == "off"
    h.sandbox(agent, True)
    h.until_value(agent, "state", "on")


def test_nothing_to_lock_on_to(agent):
    """With the camera turned away from both, the key locks on to nothing (and puts the camera behind your cat)."""
    agent.lua("""local r = Game.controller():GetControlRotation()
        Game.controller():SetControlRotation({ Pitch = r.Pitch, Yaw = WCMODS_SCENE_YAW + 180, Roll = 0 }) return true""")
    time.sleep(0.3)
    h.action(agent, "lock")
    time.sleep(0.5)
    assert h.get(agent, "locked") is False


def _wheel(agent, clicks):
    """Turn the mouse wheel in the game's window (up for clicks > 0), as Windows tells a window."""
    from wcmods.agent import window
    hwnd = window.game_window()
    wm_mousewheel = 0x020A
    delta = (120 * clicks) & 0xFFFF
    ctypes.windll.user32.PostMessageW(hwnd, wm_mousewheel, delta << 16, (400 << 16) | 400)


def test_the_real_keys(agent):
    """Tab and the mouse wheel, through the game's own input: Tab locks on, the wheel up moves to the one to the
    right and down back, Tab lets go."""
    agent.key("Tab")
    h.until_value(agent, "locked", True)
    assert h.get(agent, "target") == IDS["ahead"]
    _wheel(agent, 1)
    h.until_value(agent, "target", IDS["right"])
    _wheel(agent, -1)
    h.until_value(agent, "target", IDS["ahead"])
    agent.key("Tab")
    h.until_value(agent, "locked", False)
