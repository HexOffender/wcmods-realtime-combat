"""Step 3, attacks (docs/plan.md): F a light attack, G a heavy one, a lunge while running. Each plays its strike, costs
stamina, and when it lands hits one animal in front of your cat (the heavy attack, a wide swing, every one); the animal's health (the mod's own) goes
down by the rules' damage, and at none left it plays its "lose" animation and is taken away. A light attack pressed
near the end of one follows it; with no stamina, none is made. Rabbits are put out held still: one in front of your
cat, one behind.

Run as test_sandbox_gate.py says."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402

PLACES = {"front": (0, 140), "behind": (180, 160)}
IDS = {}
LEVEL_DAMAGE = "(8 + 1.1 * math.min(60, math.max(1, Game.cat():level())))"   # rules.hpp's cat_damage, light


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent)
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    IDS.clear()
    IDS.update(h.scene(agent, PLACES))
    agent.lua("Game.cat():setStamina(100) return true")


def teardown(agent):
    h.until(agent, f'{h.MOD}.get("attack") == ""', 5, "the attack to end")
    if h.get(agent, "locked"):
        h.action(agent, "lock")
    h.clear_scene(agent)
    h.restore_sandbox(agent)


def _hp(agent, name):
    return h.get(agent, f"hp {IDS[name]}")


def _light_damage(agent):
    return agent.lua(f"return {LEVEL_DAMAGE}")


def _attack(agent, name):
    """An attack, and wait for it to end. How many were made before it."""
    before = h.get(agent, "attacks") or 0
    h.action(agent, name)
    h.until(agent, f'({h.MOD}.get("attacks") or 0) > {before}', 2, f"the {name} attack to start")
    h.until(agent, f'{h.MOD}.get("attack") == ""', 3, f"the {name} attack to end")
    return before


def test_a_light_attack(agent):
    """F: Quick Claw's strike plays, costs 12 stamina, and hits the rabbit in front (not the one behind) for the light
    attack's damage."""
    before = h.get(agent, "attacks") or 0
    h.action(agent, "light")
    h.until(agent, f'({h.MOD}.get("attacks") or 0) > {before}', 2, "the attack to start")
    assert h.get(agent, "attack") == "light"
    playing = agent.lua('local m = Game.get(Game.player(), "Mesh"):GetAnimInstance():GetCurrentActiveMontage() '
                        'if m and m:IsValid() then return m:GetFName():ToString() end return nil')
    assert playing == "AM_UM_Quick_Claw", playing
    h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the attack to end")
    stamina = agent.lua("return Game.cat():stamina()")   # first: some comes back 0.6 s after
    assert h.get(agent, "last_hit") == IDS["front"]
    damage = _light_damage(agent)
    hp, top = _hp(agent, "front"), h.get(agent, f"hp_max {IDS['front']}")
    assert top and abs((top - hp) - damage) < 0.5, (top, hp, damage)
    assert h.get(agent, f"hp {IDS['behind']}") is None, "the rabbit behind was hit"
    assert 86 <= stamina <= 90, f"stamina {stamina} just after a light attack (100 less 12, none back for 0.6 s)"


def test_a_heavy_attack_does_more(agent):
    """G: Strong Claw, 1.9 times the light attack's damage, 25 stamina."""
    _attack(agent, "heavy")
    stamina = agent.lua("return Game.cat():stamina()")
    damage = _light_damage(agent) * 1.9
    hp, top = _hp(agent, "front"), h.get(agent, f"hp_max {IDS['front']}")
    assert abs((top - hp) - damage) < 0.5, (top, hp, damage)
    assert 73 <= stamina <= 78, f"stamina {stamina} just after a heavy attack (100 less 25)"


def test_light_attacks_follow_on(agent):
    """A light attack pressed near the end of one follows it, with no press after."""
    before = h.get(agent, "attacks") or 0
    h.action(agent, "light")
    time.sleep(0.45)          # once it's landed (0.38 s into its 0.73 s)
    h.action(agent, "light")
    h.until(agent, f'({h.MOD}.get("attacks") or 0) >= {before + 2}', 2, "the second attack to follow")
    h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the attacks to end")


def test_no_stamina_no_attack(agent):
    """With too little stamina (under half the attack's cost), the key makes no attack."""
    refused, made = h.get(agent, "refused") or 0, h.get(agent, "attacks") or 0
    agent.lua("Game.cat():setStamina(0) return true")
    h.action(agent, "light")   # 20 a second comes back: about 3 by the time it's read
    time.sleep(0.2)
    assert (h.get(agent, "refused") or 0) == refused + 1 and (h.get(agent, "attacks") or 0) == made, (
        h.get(agent, "refused"), h.get(agent, "attacks"))
    agent.lua("Game.cat():setStamina(100) return true")


def test_defeat(agent):
    """Hit till its health is gone: it plays its "lose" animation, the lock lets go, and it's taken away."""
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["front"])
    defeated = h.get(agent, "defeated") or 0
    for _ in range(8):
        # back in front of your cat (each hit pushes it away), and stamina for the next
        agent.lua("""local me, yaw = Game.cat():location(), math.rad(Game.player():K2_GetActorRotation().Yaw)
            WCMODS_SCENE.front:K2_SetActorLocation({ X = me.X + math.cos(yaw) * 140, Y = me.Y + math.sin(yaw) * 140, Z = me.Z },
                false, {}, true)
            Game.cat():setStamina(100) return true""")
        time.sleep(0.2)
        _attack(agent, "heavy")
        if (h.get(agent, "defeated") or 0) > defeated:
            break
    assert h.get(agent, "last_defeated") == IDS["front"] and _hp(agent, "front") == 0
    h.until_value(agent, "locked", False)
    assert h.get(agent, "released") == "defeated"
    playing = agent.lua('local m = Game.get(WCMODS_SCENE.front, "Mesh"):GetAnimInstance():GetCurrentActiveMontage() '
                        'if m and m:IsValid() then return m:GetFName():ToString() end return nil')
    assert playing and "Lose" in playing, playing
    h.until(agent, "not Game.alive(WCMODS_SCENE.front)", 8, "the defeated rabbit to be taken away")


def test_lunge(agent):
    """The lunge carries your cat forward as it strikes."""
    agent.lua("Game.animals.remove(WCMODS_SCENE.front) WCMODS_SCENE.front = nil return true")   # a clear way ahead
    time.sleep(0.3)
    start = agent.lua("local p = Game.cat():location() return { p.X, p.Y }")
    _attack(agent, "lunge")
    end = agent.lua("local p = Game.cat():location() return { p.X, p.Y }")
    moved = ((end[0] - start[0]) ** 2 + (end[1] - start[1]) ** 2) ** 0.5
    assert moved > 100, f"the lunge moved your cat {moved:.0f} units"


def test_your_cat_moves_after_an_attack(agent):
    """An attack leaves your cat free to walk once it's done (the game's animation blocks input while it plays)."""
    agent.lua("Game.animals.remove(WCMODS_SCENE.front) WCMODS_SCENE.front = nil return true")   # a clear way ahead
    _attack(agent, "light")
    start = agent.lua("local p = Game.cat():location() return { p.X, p.Y }")
    agent.keys("W", hold=1.0)
    end = agent.lua("local p = Game.cat():location() return { p.X, p.Y }")
    moved = ((end[0] - start[0]) ** 2 + (end[1] - start[1]) ** 2) ** 0.5
    assert moved > 60, f"your cat walked {moved:.0f} units in a second after an attack"


def test_the_real_keys(agent):
    """F and G, through the game's own input."""
    for key, name in (("F", "light"), ("G", "heavy")):
        agent.lua("Game.cat():setStamina(100) return true")
        before = h.get(agent, "attacks") or 0
        agent.key(key)
        h.until(agent, f'({h.MOD}.get("attacks") or 0) > {before}', 2, f"{key} to attack")
        assert h.get(agent, "attack") == name
        h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the attack to end")


def test_a_strike_steps_in_to_reach(agent):
    """Locked on to a rabbit just out of reach (2.9 m, where the animals pace), a light attack steps in as it comes
    down and lands; your cat ends up to 1.2 m nearer. (A new scene: last in this file.)"""
    IDS.clear()
    IDS.update(h.scene(agent, {"far": (0, 290), "behind": (180, 160)}))
    agent.lua("Game.cat():setStamina(100) return true")
    h.action(agent, "lock")
    h.until_value(agent, "target", IDS["far"])
    hp = _hp(agent, "far")
    start = agent.lua("local p = Game.cat():location() return { p.X, p.Y }")
    _attack(agent, "light")
    end = agent.lua("local p = Game.cat():location() return { p.X, p.Y }")
    moved = ((end[0] - start[0]) ** 2 + (end[1] - start[1]) ** 2) ** 0.5
    h.action(agent, "lock")
    assert _hp(agent, "far") < hp, f"the strike missed the rabbit 2.9 m off (it has {_hp(agent, 'far')} of {hp})"
    assert 60 < moved < 160, f"your cat stepped {moved:.0f} units"
