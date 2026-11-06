"""Step 1, the Sandbox gate (docs/plan.md): real-time combat is on only for a Sandbox mode cat. Sandbox mode hands
its state to C++ mods (WCModsCpp.share, 0.7 and later); while the mod is on, the roaming animals ignore your cat, so
none starts a turn-based fight, and a pounce starts none either. Off, they notice your cat again.

Run with wcmods' agent, the mod and Sandbox mode 0.7 installed:

    python <wcmods>/wcmods.py agent install --with <this repo>/realtime-combat --with <wcmods-sandbox>/sandbox
    python <wcmods>/wcmods.py agent test tests/game

This file's name puts it last in a run: one of its tests plays one of the game's own fights (and flees), and after a
fight the game moves the view to a new exploration camera only a while later, so for a time the camera doesn't follow
a turn made by a script (seen in game; a player with the mod on gets no such fight).

The tests say whether the cat in play is a sandbox cat by sharing "Sandbox.ready" from the agent's Lua bridge, as
Sandbox mode would, and put back what Sandbox mode itself says after each. (The dev save's cat, slot 8, is a sandbox
cat: Sandbox mode clears its story quests as it comes into the forest, so put the save back after a run.)"""
import os
import sys
import time

from wcmods.agent import skip

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MOD = 'WCModsCpp.mods["realtime-combat"]'

# An animal out where the animals live (beside a den no pounce starts a fight): 2,500 units or more from your cat, on
# level ground, one not tried yet; WCMODS_PREY and a spot 300 units from it, WCMODS_SPOT.
OUT = """
local me = Game.cat():location()
WCMODS_TRIED = WCMODS_TRIED or {}
for _, n in ipairs(Game.animals.near(20000)) do
    local p = n.actor:K2_GetActorLocation()
    if n.distance > 2500 and math.abs(p.Z - me.Z) < 400 and not WCMODS_TRIED[n.actor:GetAddress()] then
        WCMODS_TRIED[n.actor:GetAddress()] = true
        local g = Game.ground(p.X + 300, p.Y, p.Z + 300)
        if g and math.abs(g - p.Z) < 100 then
            WCMODS_PREY = n.actor
            WCMODS_SPOT = { X = p.X + 300, Y = p.Y, Z = g + 20 }
            return true
        end
    end
end
return "no animal out where they live"
"""


def _state(agent):
    return agent.lua(f'local m = {MOD} return {{ on = m.get("on"), state = m.get("state") }}')


def _sandbox(agent, ready):
    agent.lua(f'WCModsCpp.share("Sandbox.ready", {"true" if ready else "false"}) return true')


def _sandbox_says(agent):
    """What Sandbox mode itself shares (UE4SS's shared variable): whether the cat in play is a sandbox cat, being
    played; None without Sandbox mode."""
    got = agent.lua('return { installed = ModRef:GetSharedVariable("Sandbox.version") ~= nil, '
                    'ready = ModRef:GetSharedVariable("Sandbox.ready") == true }')
    return got["ready"] if got.get("installed") else None


def _sandbox_settled(agent):
    """Wait (up to 20 s) until Sandbox mode is done deciding about the cat in play: a sandbox cat coming into the
    forest is "active" a few seconds before it's "ready" (its opening scenes, the quests cleared)."""
    end = time.time() + 20
    while time.time() < end and agent.lua('return ModRef:GetSharedVariable("Sandbox.active") == true and '
                                          'ModRef:GetSharedVariable("Sandbox.ready") ~= true'):
        time.sleep(0.5)
    time.sleep(0.5)   # the mod looks four times a second


def _until(agent, cond, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        if agent.lua("return " + cond):
            return
        time.sleep(0.25)
    raise AssertionError(f"timed out after {timeout} s waiting for {what}: {_state(agent)}")


def _until_state(agent, state, timeout=5):
    _until(agent, f'{MOD}.get("state") == "{state}"', timeout, f'the mod to be "{state}"')


def _ignoring(agent):
    """Each roaming animal within 4,000 units, by its address: whether it ignores your cat."""
    return agent.lua("""local out = {}
        for _, n in ipairs(Game.animals.near(4000)) do
            out[string.format("%x", n.actor:GetAddress())] = Game.ai.ignoringPlayer(n.actor)
        end
        return out""") or {}


def _leave_fight(agent):
    """Flee a fight that's on, answering any dilemma, until your cat is back in the world."""
    end = time.time() + 45
    while time.time() < end and agent.lua("return Game.combat.active()"):
        if agent.lua("return Game.combat.dilemma() ~= nil"):
            agent.lua("return Game.combat.answerDilemma(1)")
        agent.lua("return Game.combat.flee()")
        time.sleep(2)
    _until(agent, "Game.player() ~= nil and not Game.combat.active()", 30, "your cat back in the world")


def _pounces(agent, animals=2, tries=2, wait=6):
    """Pounce on up to `animals` animals out where they live, `tries` times each, your cat first put beside each and
    left there a moment (the mod looks for animals to ignore once a second, as one comes near). Whether a fight
    started."""
    agent.lua("WCMODS_TRIED = nil return true")
    for _ in range(animals):
        got = agent.lua(OUT)
        if got is not True:
            skip(str(got))
        agent.lua("local s = WCMODS_SPOT return Game.cat():teleport(s.X, s.Y, s.Z + 40)")
        time.sleep(1.5)
        for _ in range(tries):
            if agent.lua("return Game.combat.active()"):
                return True   # it chased your cat into one as it came near
            if agent.lua("return Game.combat.start(WCMODS_PREY)") is not True:
                break         # it's gone (or your cat's in a menu): the next animal
            end = time.time() + wait
            while time.time() < end:
                if agent.lua("return Game.combat.active()"):
                    return True
                time.sleep(0.5)
    return False


def setup(agent):
    assert agent.lua(f"return {MOD} ~= nil"), "Real-time combat isn't running: install it (this file's docstring)"
    agent.hotkey("Home", focus=True)   # the game in front: behind other windows it barely ticks, and pauses
    agent.lua("WCMODS_HOME = WCMODS_HOME or Game.cat():location() return true")


def teardown(agent):
    _sandbox(agent, _sandbox_says(agent) is True)
    if agent.lua("return Game.combat.active()"):
        _leave_fight(agent)
    agent.lua("local h = WCMODS_HOME Game.cat():teleport(h.X, h.Y, h.Z) return true")


def test_sandbox_mode_reaches_cpp(agent):
    """Sandbox mode 0.7 hands its state to C++ mods: the mod is on for a sandbox cat and off for a story cat, as
    Sandbox mode itself says ("no-sandbox" would mean it never heard from it)."""
    _sandbox_settled(agent)
    says = _sandbox_says(agent)
    assert says is not None, "Sandbox mode (0.7 or later) isn't installed"
    s = _state(agent)
    assert s["state"] != "no-sandbox", "Sandbox mode's state didn't reach C++"
    assert s == ({"on": True, "state": "on"} if says else {"on": False, "state": "not-sandbox"}), (says, s)


def test_on_for_a_sandbox_cat(agent):
    """A sandbox cat: on, and every animal near ignores your cat. Not one any more: off, and they notice it again."""
    _sandbox(agent, False)
    _until_state(agent, "not-sandbox")
    before = _ignoring(agent)
    _sandbox(agent, True)
    _until_state(agent, "on")
    assert _state(agent)["on"] is True
    _until(agent, "(function() for _, n in ipairs(Game.animals.near(4000)) do "
                  "if not Game.ai.ignoringPlayer(n.actor) then return false end end return true end)()",
           3, "every animal near to ignore your cat")
    _sandbox(agent, False)
    _until_state(agent, "not-sandbox")
    assert _state(agent)["on"] is False
    # the animals still near (they roam, so some may have come or gone): each as it was before
    after = _ignoring(agent)
    changed = {a: (before[a], after[a]) for a in before.keys() & after.keys() if before[a] != after[a]}
    assert before.keys() & after.keys(), "no animal stayed near to compare"
    assert not changed, f"these animals don't notice your cat as before (before, after): {changed}"


def test_no_fight_while_on(agent):
    """While it's on, pounces on animals out where they live start no turn-based fight; once it's off, one does."""
    _sandbox(agent, True)
    _until_state(agent, "on")
    assert not _pounces(agent), "a pounce started a turn-based fight while real-time combat was on"
    _sandbox(agent, False)
    _until_state(agent, "not-sandbox")
    if not _pounces(agent, animals=3):
        skip("no pounce started a fight with the mod off either, so this spot proves nothing")
    _leave_fight(agent)
    agent.lua("local h = WCMODS_HOME Game.cat():teleport(h.X, h.Y, h.Z) Game.save.write() return true")


def test_walking_into_an_animal(agent):
    """While it's on, your cat walking into an animal starts no fight (the game's encounter capsule is off); one it's
    fighting (locked on to) it bumps into rather than passing through."""
    import helpers as h
    _sandbox(agent, True)
    _until_state(agent, "on")
    h.scene(agent, {"rabbit": (0, 120)})
    try:
        agent.keys("W", hold=1.5)
        time.sleep(0.5)
        assert not agent.lua("return Game.combat.active()"), "walking into the rabbit started a turn-based fight"
        agent.lua("local h = WCMODS_HOME Game.cat():teleport(h.X, h.Y, h.Z) return true")
        h.action(agent, "lock")
        h.until(agent, f'({h.MOD}.get("blocking") or 0) >= 1', 2, "its body to block")
        agent.keys("W", hold=1.5)
        time.sleep(0.5)
        assert not agent.lua("return Game.combat.active()"), "walking into the rabbit started a turn-based fight"
        # how far past the rabbit your cat got, along the way it walked (below 0: still this side of it)
        past = agent.lua("""local a, me = WCMODS_SCENE.rabbit:K2_GetActorLocation(), Game.cat():location()
            local yaw = math.rad(WCMODS_SCENE_YAW)
            return (me.X - a.X) * math.cos(yaw) + (me.Y - a.Y) * math.sin(yaw)""")
        assert past < -20, f"your cat got {past:.0f} units past the rabbit's middle: it went through it"
    finally:
        if h.get(agent, "locked"):
            h.action(agent, "lock")
        h.clear_scene(agent)


def test_switched_off_in_its_settings(agent):
    """The mod menu's page: it says what's on and why not, and its switch turns real-time combat off for a sandbox
    cat (and back on), kept in settings.json."""
    _sandbox(agent, True)
    _until_state(agent, "on")
    agent.menu.open()
    opened = False
    try:
        agent.menu.open("Real-time combat")
        opened = True
        page = agent.menu.read()
        assert page.get("Now") == "On", page.text()
        agent.menu.set("Real-time combat in Sandbox mode", False)
        _until_state(agent, "disabled")
        assert _state(agent)["on"] is False
        assert agent.menu.read().get("Now") == "Off: switched off in its settings"
        agent.menu.set("Real-time combat in Sandbox mode", True)
        _until_state(agent, "on")
    finally:
        if opened:
            agent.menu.set("Real-time combat in Sandbox mode", True)
        agent.menu.close()
