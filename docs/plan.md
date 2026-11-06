# Real-time combat: the plan

A C++ mod for *Warrior Cats: Clans of the Forest*, built with wcmods' C++ SDK, that swaps the game's
turn-based fights for real-time ones while you play a Sandbox mode cat: you lock on to an animal,
attack it, dodge its attacks and drink from a healing flask, in the open world, with no fight screen.
It's only on in Sandbox mode ([wcmods-sandbox](https://github.com/HexOffender/wcmods-sandbox)); story
cats keep the game's own fights.

Started 2026-11-06. Each step is a pull request with its own in-game tests, reviewed and merged before
the next.

## What the game gives us (checked in game, 2026-11-06)

- **Animations:** each cat's montage table (`DT_Cat_CombatAnimations`, on its `PlayMontageComponent`)
  has the attacks from the fights (`Gameplay.Combat.Attack.Universal.1` to `.20`: Taunt, QuickClaw,
  strongclaw, gigastomp, Tackle, JumpAttack, TailAttack...; and each Clan's ten), the hit reactions
  (`Animation.Combat.Hit.1.Neutral`...) and `Animation.Combat.Dodge.Left` / `.Right`. Each animal has
  its own table (`DT_WeakMice_CombatAnimations`: `Gameplay.Combat.Attack.WeakMice.1` RodentBite, `.2`
  Tackle, `.3` SandThrow, the hit reactions, the dodges, `Animation.Combat.Lose.1` and `.2`). Played by
  tag on the open world's actors (`wcmods::play_montage`), they play: `AM_Dodge_L` on your cat,
  `AM_Mice_Rodent_Bite` on a mouse.
- **The dodge doesn't move the cat** (no root motion): it stayed on its spot. The mod moves it.
  `LaunchCharacter` does (a sideways shove moved it about a metre).
- **The camera follows the controller's rotation:** `SetControlRotation` turned the view 90 degrees. Lock-on
  turns it toward the target each frame.
- **Animals have no health out of a fight.** Their stats only exist once a turn-based fight starts
  (`WrcTurnBasedCombatComponent`), so an animal's health in a real-time fight is the mod's own, scaled
  from its level the way the game scales a fighter's.
- **Your cat's health is the game's** (`Cat::health()`, 178 of 178 on the test cat, on the HUD), and so is
  its stamina (`Cat::stamina()`, which running uses). The mod takes damage off and heals that health,
  and spends that stamina.
- **The turn-based fights start two ways:** an animal that notices your cat chases it and catches it
  within about 90 units, or your cat pounces on one (SpaceBar). `wcmods::ignore_player_everywhere(true)`
  stops the first, and the second too: six pounces on animals ignoring your cat started no fight, and the
  first pounce with them noticing it did (step 1's test checks both).
- **Keys:** most letters are already the game's. The left mouse button is Interact (it travels at a den
  entrance), the right one crouches, R resets the camera, Q tracks scents, SpaceBar pounces. Free: F, G,
  H, Z, B, N, Tab, Left Alt, the middle mouse button, 5 to 9.

## How it fits together

- **The Sandbox gate.** The Sandbox mode mod is Lua and shares its state through UE4SS's shared
  variables, which a C++ mod can't read. Step 1 makes it share the same values with C++ mods too
  (`WCModsCpp.share("Sandbox.ready", ...)`, read with `wcmods::shared`). UE4SS starts C++ mods before
  Lua ones, so `WCModsCpp` is there when the Sandbox mod's script runs. The mod is on while
  `Sandbox.ready` is true and puts everything back when it goes false.
- **Rules apart from the game.** Damage, stamina costs, hit cones, target picking and the enemies'
  timings are plain C++ in `src/rules.hpp`, with no UE4SS in it, so they have unit tests that build and
  run anywhere (and on GitHub's CI). The game-facing code calls them.
- **Tests.** In-game tests in `tests/game/` run with wcmods' agent
  (`python <wcmods>/wcmods.py agent test tests/game`). They turn the mod on by sharing `Sandbox.ready`
  from the agent's Lua bridge, so they run on the dev save's cat; step 1 and the last step also check it
  with the real Sandbox mode mod and a sandbox cat. The mod shares its own state
  (`WCModsCpp.mods["realtime-combat"].get(...)`: the target, the enemies' health, the flask) for the
  tests to read.
- **Settings.** `settings.json` holds the keys and the numbers (damage, stamina costs, flask charges,
  how far lock-on reaches). The mod menu's page shows them, and they can be changed while you play.

## Keys (the defaults; each is a setting)

| Action | Key | Why that one |
|---|---|---|
| Lock on, switch off | Tab, middle mouse button | Nothing of the game's on either |
| Next target | mouse wheel while locked on | Not one of the game's |
| Light attack | F | Beside WASD; the left mouse button is Interact |
| Heavy attack | G | |
| Dodge | Left Alt | SpaceBar pounces |
| Drink from the flask | Z | R resets the camera |

Controller buttons come in step 8, once it's checked which ones the game leaves free in play (most face
buttons and shoulders are taken).

## Steps

| Step | Status |
|---|---|
| 1. The mod, and the Sandbox gate | done: #1, with Sandbox mode 0.7 (HexOffender/wcmods-sandbox#7) |
| 2. Lock-on | |
| 3. Attacks, stamina and enemy health | |
| 4. Dodge | |
| 5. Enemies fight back | |
| 6. The flask | |
| 7. The HUD, and falling | |
| 8. The controller, settings, and the release | |

### 1. The mod, and the Sandbox gate

- A new repository, `HexOffender/wcmods-realtime-combat`, with the mod in `realtime-combat/` (made from
  wcmods-examples' C++ template: `mod.json`, `settings.json`, `cpp/`, the Visual Studio solution).
- wcmods-sandbox: `share()` also hands each value to C++ mods (`WCModsCpp.share`). A pull request there,
  with the Sandbox mod's version bumped.
- The mod: on while `Sandbox.ready` (and its own "Real-time combat" setting) is on. While it's on, the
  animals ignore your cat (`ignore_player_everywhere`), so none starts a turn-based fight; off, they
  notice it again. A pounce on an animal ignoring your cat starts no fight either (seen in game), so
  nothing more is needed for pounces.
- A page in the mod menu: whether it's on, and why not.
- Tests: on in sandbox, off out of it; an animal walked into doesn't start a fight while on, and does
  while off; a pounce doesn't start one; with the real Sandbox mode mod on a sandbox cat, it's on.

### 2. Lock-on

- Tab picks the animal nearest the middle of the view, within 15 m and in sight (a trace from your cat's
  head to its body). The camera turns toward it every frame, easing, with the pitch kept a little down,
  and your cat turns to face it as it attacks. Tab again lets go; so does the animal going out of reach,
  out of sight for 2 s, or dying.
- The mouse wheel moves to the next animal, left or right of the one now.
- A marker on the target (an overlay on its body, projected each frame).
- Tests: the nearest-to-the-middle animal is picked, the camera turns to it, the wheel switches, out of
  reach lets go.

### 3. Attacks, stamina and enemy health

- F is a light attack (`QuickClaw`), G a heavy one (`strongclaw`); F while running is a lunge
  (`JumpAttack`). Each has a stamina cost, a wind-up, a moment it lands and a recovery (in `rules.hpp`,
  set per attack after watching the animation). Your cat can't move or start another while one plays,
  except that a light attack pressed during the last part of the one before chains into it.
- A hit is the animal in a cone in front of your cat within reach at the moment the attack lands. The
  damage comes from your cat's level and stats, and the animal plays a hit reaction and is pushed back.
- An animal's health: its level (the kind's, `...L6`, or its generator's party) through the game's own
  stats for its kind (`base + growth x level`), times a setting.
- Stamina: the game's own, as running uses it; an attack with too little isn't made. It comes back as it
  does in the game.
- Tests: an attack plays its animation, costs stamina, hits an animal in front and misses one behind;
  health goes down by the rules' amount; no stamina, no attack.

### 4. Dodge

- Left Alt rolls your cat the way it's moving (or back, standing still): `Dodge_L` or `Dodge_R` by the
  side, the cat launched along the ground about 3 m. For the first part of it nothing hurts your cat. It
  costs stamina and can't be done during an attack's wind-up.
- Tests: a dodge moves the cat about 3 m the right way, costs stamina, and a hit in its first part does
  nothing.

### 5. Enemies fight back

- An animal you hit or come near (a few metres) fights: its own logic stops (`stop_ai`), and the mod
  moves it (`move_to`, `face`): it comes close, circles a little, and attacks with its own animations
  (each kind's `Gameplay.Combat.Attack.<Kind>.N`), each with a wind-up long enough to see coming. Its
  hit is a cone in front of it; it takes your cat's health and plays a hit reaction on your cat.
- Its damage from its level, as its health is. Several can fight at once; each waits its turn a little.
- An animal at no health plays `Lose`, is taken away, and gives what a won fight would (the game's
  experience, if the SDK can give it; to be found), and its generator puts a new one out later. One
  you run from (far, for long) goes back to roaming (its logic restarted).
- Tests: an animal near starts fighting, its attack takes health, a dodge avoids it, it dies at no
  health and is gone, one left behind roams again.

### 6. The flask

- Z drinks: a short animation (one of the cat's own exploration ones), then health back (40% of the
  most, a setting) over a second. It has charges (3, a setting); a hit while drinking stops it and the
  charge is spent. The charges fill again at camp (the game's return to camp, sleeping in the den) and
  when your cat falls.
- It's not an item in the inventory: the charges are the mod's, kept per sandbox cat in a file of its own
  beside the save.
- Tests: drinking heals by the right amount and spends a charge, none left does nothing, a hit stops it,
  the charges fill at camp.

### 7. The HUD, and falling

- On screen: the target's health bar (its name and level above it), your cat's stamina under its health
  bar, the flask and its charges.
- Your cat at no health doesn't die the game's way (unknown outside a fight): the mod catches it first,
  plays `Lose`, fades out, and sends your cat back to camp (the game's own return), full health, the flask
  filled, the animals that were fighting back to roaming.
- Tests: the bars show and follow the numbers; falling sends your cat to camp, healed.

### 8. The controller, settings, and the release

- Controller buttons (after checking which the game leaves free in play), the settings page finished
  (keys, damage, health and stamina multipliers, flask charges, lock-on reach), README with how to play.
- An end-to-end run in a sandbox cat, with the real Sandbox mode mod.
- `tools/pack.py` makes the release's zip. The GitHub release itself waits for a yes.

## Open questions, found along the way

- ~~Whether a pounce starts a fight while the animals ignore your cat~~: it doesn't (step 1). Step 5 checks
  it again for an animal the mod is moving (its own logic stopped).
- What the game does with a cat at no health outside a fight (step 7; the mod steps in first either way).
- Whether the game's experience can be given from C++ (step 5); if not, rewards are items.
- The game's attacks were made for staged fights: some (throws, sand, rocks) may look odd up close, and
  are left out of the moves until they're seen.
