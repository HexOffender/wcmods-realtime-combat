"""What the in-game tests share: reading the mod's state, saying Sandbox mode is on, doing an action as its key would,
waiting, and a scene of animals held still at known places."""
import itertools
import time

MOD = 'WCModsCpp.mods["realtime-combat"]'
RABBIT = "Gameplay.Combat.Generator.Fixed.Rabbit.01.L6"
_count = itertools.count(1)


def get(agent, name):
    """One of the mod's shared values."""
    return agent.lua(f'return {MOD}.get("{name}")')


def sandbox(agent, ready):
    """Say (to C++ mods) whether the cat in play is a sandbox cat being played, as Sandbox mode does."""
    agent.lua(f'WCModsCpp.share("Sandbox.ready", {"true" if ready else "false"}) return true')


def sandbox_says(agent):
    """What Sandbox mode itself shares: whether the cat in play is a sandbox cat, being played; None without it."""
    got = agent.lua('return { installed = ModRef:GetSharedVariable("Sandbox.version") ~= nil, '
                    'ready = ModRef:GetSharedVariable("Sandbox.ready") == true }')
    return got["ready"] if got.get("installed") else None


def action(agent, name):
    """Do one of the mod's actions as its key would ("lock", "next_right"...), through its test channel."""
    agent.lua(f'WCModsCpp.share("RealtimeCombat.action", "{name} {next(_count)}") return true')
    time.sleep(0.15)   # it's read each frame


def until(agent, cond, timeout, what):
    """Wait for a Lua condition (an expression)."""
    end = time.time() + timeout
    while time.time() < end:
        if agent.lua("return " + cond):
            return
        time.sleep(0.1)
    raise AssertionError(f"timed out after {timeout} s waiting for {what}")


def until_value(agent, name, want, timeout=3):
    end = time.time() + timeout
    got = None
    while time.time() < end:
        got = get(agent, name)
        if got == want:
            return
        time.sleep(0.1)
    raise AssertionError(f'the mod\'s "{name}" is {got!r}, not {want!r}, after {timeout} s')


def turn_on(agent):
    """The mod on: the game in front, Sandbox mode saying so."""
    agent.hotkey("Home", focus=True)
    sandbox(agent, True)
    until_value(agent, "state", "on", 5)


def restore_sandbox(agent):
    sandbox(agent, sandbox_says(agent) is True)


# Animals put at places around your cat, held still (their logic stopped): WCMODS_SCENE[name] = the actor. Your
# cat faces along the camera first. Any other animal within 3,000 units is taken out of the world (the game's
# generators bring theirs back later), so only the scene's count.
SCENE = """
WCMODS_HOME = WCMODS_HOME or Game.cat():location()
local me = Game.cat():location()
local places = %(places)s
-- a way to face where every place is on ground about level with your cat (at the test cat's spot the ground drops
-- away one way), tried from the camera's yaw round in eighths; the camera turned to it
local start = Game.controller():GetControlRotation().Yaw
local yaw, spots
for k = 0, 7 do
    local try, ok = start + k * 45, {}
    for name, place in pairs(places) do
        local r = math.rad(try + place[1])
        local x, y = me.X + math.cos(r) * place[2], me.Y + math.sin(r) * place[2]
        local g = Game.ground(x, y, me.Z + 300)
        if not g or math.abs(g - me.Z) > 120 then ok = nil break end
        ok[name] = { X = x, Y = y, Z = g + 30 }
    end
    if ok then yaw, spots = try, ok break end
end
if not yaw then return "no level ground round your cat for the scene" end
local pitch = Game.controller():GetControlRotation().Pitch
Game.controller():SetControlRotation({ Pitch = pitch, Yaw = yaw, Roll = 0 })
Game.player():K2_SetActorRotation({ Pitch = 0, Yaw = yaw, Roll = 0 }, false)   -- your cat faces that way too
WCMODS_SCENE_YAW = yaw
for _, n in ipairs(Game.animals.near(3000)) do Game.animals.remove(n.actor) end
WCMODS_SCENE = {}
for name, spot in pairs(spots) do
    local a, why = Game.animals.spawn("%(kind)s", spot)
    if not a then return "couldn't spawn " .. name .. ": " .. tostring(why) end
    Game.ai.stop(a)
    WCMODS_SCENE[name] = a
end
return true
"""


def scene(agent, places, kind=RABBIT):
    """Put animals around your cat: places = {name: (degrees right of the camera, distance)}. Gives each one's
    address."""
    # the camera still first: on a fresh game it turns a while after the level loads
    end = time.time() + 10
    last = control_yaw(agent)
    while time.time() < end:
        time.sleep(0.5)
        now = control_yaw(agent)
        if abs(turn(last, now)) < 0.5:
            break
        last = now
    lua_places = "{" + ", ".join(f'{n} = {{{a}, {d}}}' for n, (a, d) in places.items()) + "}"
    got = agent.lua(SCENE % {"places": lua_places, "kind": kind}, timeout=30)
    assert got is True, got
    time.sleep(0.5)
    return agent.lua("local out = {} for n, a in pairs(WCMODS_SCENE) do out[n] = a:GetAddress() end return out")


def clear_scene(agent):
    agent.lua("""for _, a in pairs(WCMODS_SCENE or {}) do pcall(Game.animals.remove, a) end
        WCMODS_SCENE = nil
        if WCMODS_HOME then Game.cat():teleport(WCMODS_HOME.X, WCMODS_HOME.Y, WCMODS_HOME.Z) end
        return true""")


def control_yaw(agent):
    return agent.lua("return Game.controller():GetControlRotation().Yaw")


def yaw_to(agent, name):
    """The yaw from your cat to a scene animal."""
    return agent.lua(f"""local me, a = Game.cat():location(), WCMODS_SCENE["{name}"]:K2_GetActorLocation()
        return math.deg(math.atan(a.Y - me.Y, a.X - me.X))""")


def turn(a, b):
    """The short turn from yaw a to yaw b."""
    d = (b - a) % 360
    return d - 360 if d > 180 else d
