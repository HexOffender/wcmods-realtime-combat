"""Step 9, the settings (docs/plan.md): the mod menu's Real-time combat page changes how it plays at once, kept in
settings.json. Your cat's damage doubled: a light attack does twice as much. (This file runs last but for the gate's:
it opens the mod menu, after which the game's camera can stay put for a while.)

Run as test_sandbox_gate.py says."""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers as h  # noqa: E402

LIGHT = "(8 + 1.1 * math.min(60, math.max(1, Game.cat():level())))"
SETTINGS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "realtime-combat", "settings.json")
IDS = {}


def setup(agent):
    assert agent.lua(f"return {h.MOD} ~= nil"), "Real-time combat isn't running: install it (test_sandbox_gate.py)"
    h.turn_on(agent)
    IDS.clear()
    IDS.update(h.scene(agent, {"front": (0, 140)}))
    agent.lua("Game.cat():setStamina(100) return true")


def teardown(agent):
    try:
        agent.menu.open()
        agent.menu.open("Real-time combat")
        agent.menu.set("Your cat's damage", 1)
    finally:
        agent.menu.close()
    h.clear_scene(agent)
    h.restore_sandbox(agent)


def test_the_damage_slider(agent):
    """Your cat's damage set to x2 on the page: kept in settings.json, and a light attack does twice the damage."""
    agent.menu.open()
    try:
        agent.menu.open("Real-time combat")
        page = agent.menu.read()
        for row in ("Your cat's damage", "The animals fight back", "Drinks in the flask", "Lock-on reaches", "Block (just before a hit: parry)"):
            assert page.find(row), f"no row {row!r}: {page.text()}"
        agent.menu.set("Your cat's damage", 2)
    finally:
        agent.menu.close()
    time.sleep(0.8)   # written half a second after the last change
    with open(SETTINGS, encoding="utf-8") as f:
        assert json.load(f)["cat_damage"] == 2
    before = h.get(agent, "attacks") or 0
    h.action(agent, "light")
    h.until(agent, f'({h.MOD}.get("attacks") or 0) > {before}', 2, "the attack")
    h.until(agent, f'{h.MOD}.get("attack") == ""', 3, "the attack to end")
    top, hp = h.get(agent, f"hp_max {IDS['front']}"), h.get(agent, f"hp {IDS['front']}")
    light = agent.lua(f"return {LIGHT}")
    assert abs((top - hp) - min(top, light * 2)) < 0.5, f"the attack did {top - hp:.1f}, not twice {light:.1f}"
