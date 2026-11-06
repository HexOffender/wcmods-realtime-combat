# Real-time combat for Sandbox mode

A mod for *Warrior Cats: Clans of the Forest* that swaps the game's turn-based fights for real-time ones
while you play a [Sandbox mode](https://github.com/HexOffender/wcmods-sandbox) cat: lock on to an
animal, attack it, dodge what it throws at you, and heal from a flask, out in the forest with no fight
screen. Story cats keep the game's own fights.

It's a C++ mod made with [wcmods](https://github.com/HexOffender/warrior-cats-mods) and its C++ SDK.

**Work in progress.** [docs/plan.md](docs/plan.md) has the plan and what each step adds. So far, while you
play a Sandbox mode cat:
- the animals leave your cat alone, so no turn-based fight starts (step 1);
- **Tab** (or the middle mouse button) locks on to the animal nearest the middle of the view, within 15 m:
  the camera turns to it and follows it, and a marker sits on it. The mouse wheel moves to the next one to the
  right or left; Tab again lets go. Locked on, **A** and **D** walk your cat forward in a circle round the
  target (a cat can't walk sideways); W and S walk in and out (step 2);
- **F** is a light attack and **G** a heavy one; F while running (Shift) is a lunge. They cost stamina, and
  hit the animals in front of your cat; an animal's health is the mod's own, and at none left it falls and
  is gone (step 3);
- **Left Alt** dodges about 3 m the way you're moving (standing still: back); nothing hurts your cat for its
  first moment. It costs stamina, and can cut an attack short once the attack has landed (step 4).
- walking into an animal no longer starts the game's fight either: its body blocks your cat;
- **the animals fight back** (step 5): one within 4 m, or one you hit, comes after your cat and attacks with its
  own animations, one at a time, each with a wind-up you can see coming: dodge it, or hit the animal in its wind-up
  to make it reel. Beaten prey goes on your Clan's fresh-kill pile. Run far enough and it gives up;
- **Z** drinks from the flask (step 6): 40% of your cat's health back over a second, three drinks, full again when
  your cat sleeps, goes back to camp or comes into the forest. A hit while drinking stops it;
- **hold the right mouse button** to block (step 7): a hit from in front does a quarter, the rest off stamina, and
  with no stamina the guard breaks. Block just before a hit lands to **parry** it: no damage, and the animal reels,
  open to a riposte that does three times the damage. (The animations are stand-ins from your cat's fight moves.)

The mod menu (Insert) has a Real-time combat page: whether it's on, a switch for it, and the keys. The keys
and the lock-on's reach are in `realtime-combat/settings.json` (in wcmods' `mods/realtime-combat/` once it's
added), read as the game starts: restart the game after changing them.

## What it needs

- [wcmods](https://github.com/HexOffender/warrior-cats-mods), set up with the game, UE4SS installed and
  code mods allowed (`"allow_code_mods": true` in `wcmods.json`, or Options on Clan Mod Studio's Mods page).
- [Sandbox mode](https://github.com/HexOffender/wcmods-sandbox) 0.7 or later: it tells C++ mods which cat
  is a sandbox cat. Without it the mod stays off.

## Building it

A C++ mod is built against UE4SS's own source, at the version the game has. wcmods gets and builds that
once (wcmods' `docs/SDK.md`, "C++ mods", says what it needs: Visual Studio with "Desktop development
with C++", Rust and git):

```
python wcmods.py cpp setup
python wcmods.py cpp build <this repository>\realtime-combat
```

in the wcmods folder. That makes `realtime-combat\dlls\main.dll`. Or open
`realtime-combat\realtime-combat.sln` in Visual Studio: run `python tools\sync_ue4ss_props.py` in
`realtime-combat\` first (it writes `UE4SS.props` from wcmods' build), then build Release. The solution
finds wcmods beside this repository (`D:\projects\warrior-cats-mods` next to
`D:\projects\wcmods-realtime-combat`), or where `realtime-combat\wcmods.props` says.

## Putting it in the game

With the game closed, in the wcmods folder:

```
python wcmods.py add <this repository>\realtime-combat
python wcmods.py install
```

(or Apply to game in Clan Mod Studio).

## Tests

- **The rules** (`realtime-combat/cpp/src/rules.hpp`, plain C++ with nothing of the game in it):
  `python tests/unit/run.py` builds and runs `tests/unit/test_*.cpp` with g++, clang++ or Visual Studio. CI
  runs them on every pull request.
- **In the game** (`tests/game/`), with wcmods' agent: install the mod and Sandbox mode as a test build, then
  run them (wcmods' `docs/agent.md` has more):

  ```
  python wcmods.py agent install --with <this repository>\realtime-combat --with <wcmods-sandbox>\sandbox
  python wcmods.py agent test <this repository>\tests\game --launch
  ```

  They change what Sandbox mode says about the cat in play, from the agent's script bridge, and put it back
  after each test. Sandbox mode clears a sandbox cat's story quests as it comes into the forest; the run
  backs the save up first (`python wcmods.py restore <name>`, printed as the run starts), to put them back.
