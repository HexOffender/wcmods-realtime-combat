"""Step 6, the flask (docs/plan.md): Z drinks, a stand-in animation (the game's sniff) with 40% of your cat's most health
back over its second half; three charges; a hit while drinking stops it, the charge spent; no drink mid-attack; it
fills again when your cat comes into the forest or travels (the game's Return to Camp).

Run as test_sandbox_gate.py says."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent)
    h.scene(agent, {}, spawn=False)
    _refill(agent)
    agent.lua("local c = Game.cat() c:setHealth(c:maxHealth() * 0.5) c:setStamina(100) return true")


def teardown(agent):
    h.until(agent, f'not {h.MOD}.get("drinking")', 3, "the drink to end")
    agent.lua("Game.cat():heal() local h = WCMODS_HOME if h then Game.cat():teleport(h.X, h.Y, h.Z) end return true")
    _refill(agent)
    h.restore_sandbox(agent)


def _refill(agent):
    """Real-time combat off and on again (as coming into the forest): the flask's full."""
    h.sandbox(agent, False)
    h.until_value(agent, "state", "not-sandbox")
    h.sandbox(agent, True)
    h.until_value(agent, "state", "on")
    h.until(agent, f'{h.MOD}.get("flask") == {h.MOD}.get("flask_max")', 2, "the flask to be full")


def _health(agent):
    return agent.lua("local c = Game.cat() return { c:health(), c:maxHealth() }")


def _drink(agent):
    before = h.get(agent, "drinks") or 0
    h.action(agent, "drink")
    h.until(agent, f'({h.MOD}.get("drinks") or 0) > {before}', 2, "the drink to start")


def test_a_drink_heals(agent):
    """A drink: the stand-in animation, a charge spent, and 40% of your cat's most health back by its end."""
    hp, top = _health(agent)
    charges = h.get(agent, "flask")
    _drink(agent)
    playing = agent.lua('local m = Game.get(Game.player(), "Mesh"):GetAnimInstance():GetCurrentActiveMontage() '
                        'if m and m:IsValid() then return m:GetFName():ToString() end return nil')
    assert playing == "AM_Interact_Sniffing", playing
    assert h.get(agent, "flask") == charges - 1
    h.until(agent, f'not {h.MOD}.get("drinking")', 3, "the drink to end")
    now, _ = _health(agent)
    assert abs((now - hp) - top * 0.4) < 1.5, f"health {hp:.0f} to {now:.0f} of {top:.0f}: not 40% back"


def test_none_left(agent):
    """Three drinks, then the key does nothing."""
    for _ in range(3):
        agent.lua("local c = Game.cat() c:setHealth(c:maxHealth() * 0.3) return true")
        _drink(agent)
        h.until(agent, f'not {h.MOD}.get("drinking")', 3, "the drink to end")
    assert h.get(agent, "flask") == 0
    refused, drinks = h.get(agent, "flask_refused") or 0, h.get(agent, "drinks")
    h.action(agent, "drink")
    time.sleep(0.3)
    assert h.get(agent, "flask_refused") == refused + 1 and h.get(agent, "drinks") == drinks


def test_a_hit_stops_it(agent):
    """A hit partway through: the drink stops (the charge spent), with only some health back."""
    hp, top = _health(agent)
    _drink(agent)
    time.sleep(0.55)             # the healing's begun (0.45 s in)
    h.action(agent, "hurt")      # a small hit, as an animal's (5)
    h.until(agent, f'not {h.MOD}.get("drinking")', 1, "the hit to stop the drink")
    time.sleep(0.6)
    now, _ = _health(agent)
    assert now - hp < top * 0.4 - 5, f"health {hp:.0f} to {now:.0f}: the whole drink went in"
    assert h.get(agent, "flask") == 2


def test_no_drink_mid_attack(agent):
    """The key in an attack does nothing."""
    drinks = h.get(agent, "drinks") or 0
    h.action(agent, "light")
    h.action(agent, "drink")
    time.sleep(0.2)
    assert (h.get(agent, "drinks") or 0) == drinks
    h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the attack to end")


def test_travel_fills_it(agent):
    """A charge spent, then your cat taken far (as the game's Return to Camp does): the flask's full again."""
    _drink(agent)
    h.until(agent, f'not {h.MOD}.get("drinking")', 3, "the drink to end")
    assert h.get(agent, "flask") == h.get(agent, "flask_max") - 1
    moved = agent.lua("""local me = Game.cat():location()
        for k = 0, 7 do
            local yaw = math.rad(k * 45)
            local x, y = me.X + math.cos(yaw) * 4500, me.Y + math.sin(yaw) * 4500
            local g = Game.ground(x, y, me.Z + 2000)
            if g and Game.cat():teleport(x, y, g + 60) then return true end
        end
        return false""")
    assert moved, "couldn't move your cat 45 m"
    h.until(agent, f'{h.MOD}.get("flask") == {h.MOD}.get("flask_max")', 2, "the flask to fill")


def test_coming_into_the_forest_fills_it(agent):
    _drink(agent)
    h.until(agent, f'not {h.MOD}.get("drinking")', 3, "the drink to end")
    _refill(agent)


def test_the_real_key(agent):
    """Z, through the game's own input."""
    before = h.get(agent, "drinks") or 0
    agent.key("Z")
    h.until(agent, f'({h.MOD}.get("drinks") or 0) > {before}', 2, "Z to drink")


def test_sleeping_fills_it(agent):
    """A charge spent, then your cat sleeps (the game's sleep animation, as its den's Sleep plays it): full again."""
    _drink(agent)
    h.until(agent, f'not {h.MOD}.get("drinking")', 3, "the drink to end")
    assert h.get(agent, "flask") == h.get(agent, "flask_max") - 1
    playing = agent.lua("""local c = Game.get(Game.player(), "PlayMontageComponent")
        c:K2_PlayMontageOfTag(Game.tag("Animation.Exploration.Interaction.Sleep.1"), 1, true, false, false)
        local m = Game.get(Game.player(), "Mesh"):GetAnimInstance():GetCurrentActiveMontage()
        if m and m:IsValid() then return m:GetFName():ToString() end return nil""")
    assert playing and "Sleep" in playing, playing
    try:
        h.until(agent, f'{h.MOD}.get("flask") == {h.MOD}.get("flask_max")', 2, "the flask to fill")
    finally:
        agent.lua('Game.get(Game.player(), "Mesh"):GetAnimInstance():Montage_Stop(0.2, nil) return true')
