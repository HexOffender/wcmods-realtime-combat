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
  right or left; Tab again lets go (step 2).

The mod menu (Insert) has a Real-time combat page: whether it's on, a switch for it, and the keys. The keys
and the lock-on's reach are in `realtime-combat/settings.json`.

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
