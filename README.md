# Real-time combat for Sandbox mode

A mod for *Warrior Cats: Clans of the Forest* that swaps the game's turn-based fights for real-time ones
while you play a [Sandbox mode](https://github.com/HexOffender/wcmods-sandbox) cat. You lock on to an
animal and circle it, attack it, dodge, block and parry, and heal from a flask, all out in the forest with
no fight screen. Story cats keep the game's own fights.

It's a C++ mod made with [wcmods](https://github.com/HexOffender/warrior-cats-mods) and its C++ SDK.
[docs/plan.md](docs/plan.md) has how it was built, step by step, and what was found in the game along the way.

## How to play

While you play a Sandbox mode cat, the animals no longer start the game's turn-based fights, whether they
notice your cat, it pounces on one, or walks into one. Instead:

| | Keyboard and mouse | Controller |
|---|---|---|
| **Lock on** to the animal nearest the middle of the view (15 m); again to let go | Tab, or the middle mouse button | Right stick, clicked |
| **Next target** to the right or left | Mouse wheel up or down | D-pad right or left |
| **Circle** round the target, locked on (a cat walks forward round it: it can't walk sideways) | A or D | Left stick, left or right |
| **Light attack** (while running: a lunge forward) | F | RT |
| **Heavy attack**, a wide swing (every animal in front) | G | LT |
| **Dodge** the way you're moving (standing still: back) | Left Alt | B |
| **Block**, held; raised just before a hit lands: **parry** | Right mouse button | LB |
| **Drink** from the flask | Z | D-pad up |
| **The killing blow** on a [Clan life](https://github.com/HexOffender/wcmods-clan-life) cat that's yielded | H | D-pad down |

- **Stamina** is the game's own (its bar at the bottom of the screen). Attacks, dodges and blocked hits spend
  it, and none comes back while you act or for a moment after. An attack needs at least half its cost; a
  dodge needs only a scrap, so a cat can always leap aside, though a dodge from an empty bar leaves nothing
  for an attack after it.
- **Dodging:** your cat can't be hurt for the first 0.55 s of a dodge. A dodge takes it 1.5 m, a cat's hop
  (`dodge_distance_m` in the settings, 0.5 to 5). Pressed during your own attack's wind-up, the dodge waits and
  goes the moment your blow lands, so a dodge pressed at the mark is never lost to an attack already begun.
- **Attacking:** a light attack (and a lunge) lands on one animal in front of your cat: the one you're locked on
  to when it's in reach, else the nearest it can see. The heavy attack is a wide swing: it lands on every animal
  in front. A light attack pressed as one lands follows it. Locked on, a strike keeps facing its target and steps
  in toward it as it comes down, up to 1.2 m (a heavy one 0.9 m), so an animal pacing just out of reach is hit.
- **The animals fight back, each the way its kind does.** One within 4 m, or one you hit, comes toward your
  cat, but only to a couple of metres off, and paces round it, watching. They attack one at a time: one rushes
  in with a wind-up you can see coming, a yellow **!** over it from the start (don't begin an attack now),
  following your cat until the last moment, when it's committed: the **!** turns red then, and says dodge or
  block now (a dodge then is safe right through the blow). After striking it's open for a moment before it
  backs off again. Hit one in its wind-up and it reels.
  - **Some guard** when you attack them from in front (a badger most, a fox often): a guarded hit does a fifth
    of its damage and fills the animal's **posture** (the gold bar under its health; a heavy attack fills it
    twice as fast). Full, its guard breaks and it reels, open to a riposte. Keep swinging at a guard and it
    stays up, and some strike straight back. Wait it out, or get round it.
  - **Some hop aside** out of reach (mice, squirrels, birds, rabbits).
  - **Some feint**, holding the wind-up a moment to bait an early dodge (snakes most), and some strike twice in
    a row.
  - **Prey that's losing runs**, and gets away if you let it get 18 m off.
  - Its health is the mod's own, and a bar shows over it once you've hit it. The one you're locked on to gets a
    bigger bar with its name and level. Lead one 20 m from where it joined and it goes back to roaming.
- **Blocking:** a hit from in front does a quarter of its damage, and the rest comes off stamina. With too
  little stamina, the guard breaks. A **parry** takes nothing, and leaves the animal reeling and open to a
  **riposte**: your next hit on it does three times the damage.
- **The flask** has three drinks (the pips under your cat's health bar), each giving back 40% of your cat's
  health over a second. A hit while drinking stops it. It fills again when your cat sleeps, goes back to camp,
  or comes into the forest.
- **Beaten prey** goes on your Clan's fresh-kill pile.
- **Clan life's cats** (with the [Clan life](https://github.com/HexOffender/wcmods-clan-life) mod) can be fought too:
  - Strike one and it fights back as a cat does: the cats' own claws, guards, dodges, feints, double strikes and
    counters.
  - A kit only runs, and a medicine cat barely defends itself.
  - The others leave you alone unless Clan life sets one against you: exiled, your old Clanmates come at you in
    their camp.
  - Beaten, a cat yields: it goes down and waits. Walk away (or leave it a while) and it's spared; H gives the
    killing blow.
  - Clan life hears of it all: who you fought, spared or killed, and who saw it. The cats remember.
  - Your blows land on every cat and animal in front of your cat, so you can take on several at once.
- **Your party** (the clanmates Clan life lets you ask along) fights beside you:
  - Each goes after the foe you're locked on to, or the one nearest it, and strikes with the cats' claws. Its blows
    go through the foes' guards, dodges and counters as yours do.
  - It won't fight a cat of its own Clan.
  - Your blows pass it by, but a foe's swing hurts it as it hurts you. Beaten, it goes down until the fight's over,
    then gets up hurt.
  - A green bar over each one shows its health while it fights.
  - When the fight's over it follows you again, and remembers fighting beside you.
- **Falling:** at no health your cat goes down, every fight ends, and it's back at its Clan's camp, healed,
  its flask full.

The animations come from the game's own fights, cut to their strikes. The guard, the parry and the drink are
stand-ins from your cat's own moves (Stone Guard, Hissing Parry, sniffing) until they're animated properly.

**Settings:** the mod menu (Insert) has a Real-time combat page:
- whether it's on;
- your cat's damage, the animals' health and damage;
- whether the animals fight back and from how near;
- the flask;
- how far lock-on reaches.

Each change applies at once. The keys and buttons are in the mod's `settings.json`, by Unreal's key names;
restart the game after changing them.

**Two of the controller's buttons are shared with the game:** LT crouches, and B drops what your cat carries
(or talks to a friend near). The right mouse button crouches too. While blocking, the guard pose covers the
crouch.

## What it needs

- [wcmods](https://github.com/HexOffender/warrior-cats-mods), set up with the game, UE4SS installed and
  code mods allowed (`"allow_code_mods": true` in `wcmods.json`, or Options on Clan Mod Studio's Mods page).
- [Sandbox mode](https://github.com/HexOffender/wcmods-sandbox) 0.7 or later: it tells C++ mods which cat
  is a sandbox cat. Without it the mod stays off.

## Getting it

- **From a release:** download `realtime-combat-<version>.zip` from the Releases page and add it with Clan
  Mod Studio's Mods page, or `python wcmods.py add realtime-combat-<version>.zip`. Then Apply to game (or
  `python wcmods.py install`) with the game closed. The zip has the built DLL, so there's nothing to build.
- **From this repository:** build it (below), then `python wcmods.py add <this repository>\realtime-combat`.

## Building it

A C++ mod is built against UE4SS's own source, at the version the game has. wcmods gets and builds that
once. wcmods' `docs/SDK.md`, "C++ mods", says what it needs: Visual Studio with "Desktop development with
C++", Rust and git. In the wcmods folder:

```
python wcmods.py cpp setup
python wcmods.py cpp build <this repository>\realtime-combat
```

That makes `realtime-combat\dlls\main.dll`.

Or open `realtime-combat\realtime-combat.sln` in Visual Studio: run `python tools\sync_ue4ss_props.py` in
`realtime-combat\` first (it writes `UE4SS.props` from wcmods' build), then build Release. The solution finds
wcmods beside this repository (`D:\projects\warrior-cats-mods` next to `D:\projects\wcmods-realtime-combat`),
or where `realtime-combat\wcmods.props` says.

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

  They put rabbits out, held still, at places they choose, and drive the mod through shared values as well as
  its real keys. They change what Sandbox mode says about the cat in play, and put it back after each test.
  Sandbox mode clears a sandbox cat's story quests as it comes into the forest. The run backs the save up
  first (`python wcmods.py restore <name>`, printed as the run starts), so you can put them back.
- **Scripted fights** (`tools/fight_run.py`, run the same way: `python wcmods.py agent run tools/fight_run.py 50
  "dodger:fox,careful:fox"`): a scripted player fights a live titan fox or rabbit for 50 s at a time, four ways
  (never dodging; attacking all the time and dodging a quarter second after the red mark; the same but holding
  off while the mark shows; striking only in openings), with everything the mod shares logged eight times a
  second to `fight_run.json`. `python tools/analyze_fight.py` says how many strikes landed and why the rest
  missed, and for each of the animal's attacks when the mark came, when the dodge went and whether the blow
  hit. That's how the feel changes get their numbers.

## Releasing

1. Raise `"version"` in `realtime-combat/mod.json`, and build.
2. `python tools/pack.py` makes `dist/realtime-combat-<version>.zip`. It stops if the DLL is older than the
   source.
3. Make a GitHub release tagged `v<version>` with the zip attached. Sandbox mode 0.7 has to be released first.
