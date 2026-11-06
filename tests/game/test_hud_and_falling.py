"""Step 8, the HUD and falling (docs/plan.md): a health bar over each animal your cat's hit, the target's bigger with its
name and level; your cat's stamina and the flask on the HUD. At no health your cat falls: its lose animation, every
fight ended, and it's back at its Clan's camp, healed, its flask full.

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
    agent.lua("Game.cat():heal() Game.cat():setStamina(100) return true")


def teardown(agent):
    h.until(agent, f'not {h.MOD}.get("falling")', 20, "the fall to end")
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    agent.lua("Game.cat():heal() return true")
    h.restore_sandbox(agent)


def test_bars_over_animals(agent):
    """One rabbit hit: a bar over it. The other locked on to: a big one over that, with its name and level."""
    IDS.update(h.scene(agent, {"near": (0, 150), "far": (25, 600)}))
    h.until_value(agent, "hud", True)
    h.action(agent, "light")
    h.until(agent, f'{h.MOD}.get("last_hit") == {IDS["near"]}', 3, "the hit on the rabbit in front")
    h.until_value(agent, "bars", 1)
    assert not h.get(agent, "target_bar")
    # lock on to the far one: the view turned to it first
    yaw = agent.lua("""local a, me = WCMODS_SCENE.far:K2_GetActorLocation(), Game.cat():location()
        return math.deg(math.atan(a.Y - me.Y, a.X - me.X))""")
    h.set_view_yaw(agent, yaw)
    time.sleep(0.3)
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["far"])
    h.until_value(agent, "bars", 2)
    h.until_value(agent, "target_bar", True)
    agent.shot("hud-bars")


def test_falling(agent):
    """Out 40 m from camp, a hit with no health left: your cat falls (its lose animation, every fight ended), and is
    back at its Clan's camp checkpoint, healed, its flask full."""
    h.scene(agent, {}, spawn=False)
    moved = agent.lua("""local me = Game.cat():location()
        for k = 0, 7 do
            local yaw = math.rad(k * 45)
            local x, y = me.X + math.cos(yaw) * 4000, me.Y + math.sin(yaw) * 4000
            local g = Game.ground(x, y, me.Z + 2000)
            if g and Game.cat():teleport(x, y, g + 60) then return true end
        end
        return false""")
    assert moved, "couldn't move your cat away from camp"
    time.sleep(1.0)
    h.action(agent, "drink")
    h.until(agent, f'not {h.MOD}.get("drinking")', 3, "the drink to end")
    falls = h.get(agent, "falls") or 0
    agent.lua("Game.cat():setHealth(3) return true")
    h.action(agent, "hurt")   # 5: more than it has
    h.until(agent, f'({h.MOD}.get("falls") or 0) > {falls}', 2, "your cat to fall")
    playing = agent.lua('local m = Game.get(Game.player(), "Mesh"):GetAnimInstance():GetCurrentActiveMontage() '
                        'return m and m:GetFName():ToString()')
    assert playing and "Lose" in playing, playing
    assert agent.lua("return Game.cat():health()") >= 1, "your cat's health went to 0"
    h.until(agent, f'not {h.MOD}.get("falling")', 20, "your cat to be back at camp")
    camp = agent.lua("""for _, p in ipairs(Game.places.list()) do
            if p.tag == "Gameplay.Checkpoints." .. ModRef:GetSharedVariable("Sandbox.clan"):sub(1, 1)
                .. ModRef:GetSharedVariable("Sandbox.clan"):sub(2):lower() .. ".0" then return { p.at.X, p.at.Y } end
        end
        return false""")
    assert camp, "no camp checkpoint for your cat's Clan"
    at = agent.lua("local p = Game.cat():location() return { p.X, p.Y }")
    assert math.hypot(at[0] - camp[0], at[1] - camp[1]) < 300, f"your cat's at {at}, not at camp {camp}"
    health = agent.lua("local c = Game.cat() return { c:health(), c:maxHealth() }")
    assert health[0] == health[1], f"health {health}"
    assert h.get(agent, "flask") == h.get(agent, "flask_max")
