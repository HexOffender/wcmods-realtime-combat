// Real-time combat: the game's turn-based fights swapped for real-time ones while you play a Sandbox mode cat.
// docs/plan.md, in this repository, has the plan and what each step adds.
//
// The Sandbox gate (step 1): the mod is on while the Sandbox mode mod says a sandbox cat is being played (it shares
// "Sandbox.ready" with C++ mods, read here with wcmods::shared) and settings.json's "enabled" is on. While it's on,
// the roaming animals ignore your cat, so none chases it into a turn-based fight, and a pounce on one starts none
// either (seen in game: six pounces with them ignoring your cat started no fight; the first without did); and their
// bodies block your cat with their encounter capsules off (bodies.hpp), so walking into one starts none either.
//
// Lock-on (step 2): lockon.hpp. Attacks (step 3): attacks.hpp, with the animals' health in enemies.hpp and the
// stamina in stamina.hpp. The dodge (step 4): dodge.hpp. The animals fighting back (step 5): foes.hpp. The flask
// (step 6): flask.hpp. Blocking and parrying (step 7): guard.hpp, with your cat's hits taken in hurt.hpp. Each runs
// every frame while it's on.
//
// What it shares (wcmods::share), for tests and other mods to read from Lua as
// WCModsCpp.mods["realtime-combat"].get(name): "on" (true or false) and "state" (rules.hpp's code(): "on",
// "disabled", "no-sandbox"...), and lockon.hpp's.
#include <WCMods/WCMods.hpp>

#include <format>
#include <mutex>
#include <string>

#include "attacks.hpp"
#include "bodies.hpp"
#include "dodge.hpp"
#include "enemies.hpp"
#include "flask.hpp"
#include "foes.hpp"
#include "guard.hpp"
#include "hurt.hpp"
#include "game.hpp"
#include "input.hpp"
#include "lockon.hpp"
#include "rules.hpp"
#include "stamina.hpp"

namespace
{
    // Under the SDK's settings lock: reloading them replaces the whole object.
    auto enabled() -> bool
    {
        std::lock_guard lock(wcmods::settings_lock());
        return wcmods::settings()["enabled"].boolean(true);
    }

    auto set_enabled(bool on) -> void
    {
        {
            std::lock_guard lock(wcmods::settings_lock());
            wcmods::settings().set("enabled", on);
        }
        if (!wcmods::save_settings()) wcmods::log(L"couldn't write settings.json");
    }

    // The gate as it was last decided, and what's been done about it.
    struct Gate
    {
        rtc::Off off = rtc::Off::NotInWorld;
        bool on = false;   // the animals ignore your cat
    };
    auto gate() -> Gate&
    {
        static Gate g;
        return g;
    }

    auto read_gate() -> rtc::GateInput
    {
        rtc::GateInput in;
        in.enabled = enabled();
        in.sandbox_installed = !wcmods::shared("Sandbox.version").is_null();
        in.sandbox_ready = wcmods::shared("Sandbox.ready").boolean(false);
        in.in_world = wcmods::player() != nullptr;
        in.in_fight = wcmods::combat_active();
        return in;
    }

    // Looked at four times a second. Turning on makes every animal near ignore your cat (and those that come near
    // later, a look a second); turning off puts them back and lets go of a lock.
    auto update_gate() -> void
    {
        Gate& g = gate();
        const rtc::Off off = rtc::gate(read_gate());
        const bool on = off == rtc::Off::None;
        if (off != g.off) wcmods::share("state", rtc::code(off));
        if (on != g.on)
        {
            wcmods::ignore_player_everywhere(on);
            wcmods::share("on", on);
            if (on)
            {
                rtc::bodies::frame(1.0);   // the animals near set at once
                // coming into the forest (not the mod's switch turned back on, nor one of the game's fights ending)
                if (g.off == rtc::Off::NotInWorld || g.off == rtc::Off::NotSandbox || g.off == rtc::Off::NoSandbox)
                    rtc::flask::refill(L"came into the forest");
                wcmods::log(L"on: real-time combat, the animals ignore your cat");
            }
            else
            {
                rtc::lockon::release("off");
                rtc::attacks::end();
                rtc::dodge::end();
                rtc::flask::end();
                rtc::blocking::clear();
                rtc::hurt::clear();
                rtc::foes::clear();
                rtc::stamina::clear();
                rtc::bodies::clear();
                rtc::game::forget_animals();
                rtc::enemies::clear();
                wcmods::log(L"off ({}): the animals notice your cat again", rtc::why(off));
            }
            g.on = on;
        }
        g.off = off;
    }

    // Every frame while it's on: the player's actions (keys aren't looked at while it's off: they'd do nothing).
    auto frame(float dt) -> void
    {
        if (!gate().on || !wcmods::player()) return;
        rtc::game::tick(dt);
        rtc::bodies::frame(dt);
        rtc::input::Pressed pressed = rtc::input::poll();
        rtc::lockon::frame(dt, pressed);
        rtc::hurt::frame(dt);
        using rtc::input::Action;
        auto drop = [&](std::initializer_list<Action> actions) {
            for (Action a : actions) pressed.on[static_cast<size_t>(a)] = false;
        };
        // reeling from a hit, or a parry playing: nothing starts
        if (rtc::hurt::reeling() || rtc::blocking::state().parry_left > 0)
            drop({Action::Dodge, Action::Light, Action::Heavy, Action::Lunge, Action::Drink});
        // an attack or a dodge drops a guard; a drink doesn't start behind one
        if (rtc::blocking::state().guarding)
        {
            if (pressed[Action::Dodge] || pressed[Action::Light] || pressed[Action::Heavy] || pressed[Action::Lunge]) rtc::blocking::drop();
            else drop({Action::Drink});
        }
        if (rtc::flask::busy())   // no dodge or attack starts mid-drink
            for (auto a : {rtc::input::Action::Dodge, rtc::input::Action::Light, rtc::input::Action::Heavy, rtc::input::Action::Lunge})
                pressed.on[static_cast<size_t>(a)] = false;
        rtc::dodge::frame(dt, pressed);
        if (rtc::dodge::busy())   // no attack starts mid-dodge
            for (auto a : {rtc::input::Action::Light, rtc::input::Action::Heavy, rtc::input::Action::Lunge})
                pressed.on[static_cast<size_t>(a)] = false;
        rtc::attacks::frame(dt, pressed);
        rtc::flask::frame(dt, pressed, !rtc::attacks::busy() && !rtc::dodge::busy());
        rtc::blocking::frame(dt, rtc::input::held(Action::Block),
                          !rtc::attacks::busy() && !rtc::dodge::busy() && !rtc::flask::busy() && !rtc::hurt::reeling());
        if (pressed[Action::Hurt]) rtc::hurt::cat(5);
        rtc::stamina::frame(dt, rtc::attacks::busy() || rtc::dodge::busy() || rtc::blocking::state().guarding);
        // the animals fight back (tests can share RealtimeCombat.passive to keep them from it)
        if (rtc::foes::state().fight_back && !wcmods::shared("RealtimeCombat.passive").boolean(false)) rtc::foes::frame(dt);
        else if (!rtc::foes::state().all.empty()) rtc::foes::clear();
        rtc::enemies::frame(dt);
    }

    auto page(wcmods::Ui& ui) -> void
    {
        ui.heading(L"Real-time combat");
        ui.note(L"Real-time fights in place of the game's turn-based ones, while you play a Sandbox mode cat. "
                L"Work in progress: lock on, attack, dodge, block and parry, drink from the flask; the animals fight back.");
        const Gate& g = gate();
        ui.field(L"Now", g.on ? std::wstring(L"On") : std::format(L"Off: {}", rtc::why(g.off)));
        const bool was = enabled();
        const bool now = ui.on_off(L"Real-time combat in Sandbox mode", was);
        if (now != was) set_enabled(now);
        ui.field(L"Locked on to", rtc::lockon::locked() ? rtc::lockon::state().model : std::wstring(L"nothing"));
        ui.field(L"Lock on", rtc::input::keys_of(rtc::input::Action::Lock));
        ui.field(L"Next target", std::format(L"{} (right), {} (left)", rtc::input::keys_of(rtc::input::Action::NextRight),
                                             rtc::input::keys_of(rtc::input::Action::NextLeft)));
        ui.field(L"Light attack", rtc::input::keys_of(rtc::input::Action::Light) + L" (while running: a lunge)");
        ui.field(L"Heavy attack", rtc::input::keys_of(rtc::input::Action::Heavy));
        ui.field(L"Dodge", rtc::input::keys_of(rtc::input::Action::Dodge) + L" (the way you're moving; standing still, back)");
        ui.field(L"Block (just before a hit: parry)", rtc::input::keys_of(rtc::input::Action::Block) + L", held");
        ui.field(L"Drink from the flask", std::format(L"{} ({} of {} left; full again at camp, asleep, or coming into the forest)",
                                                     rtc::input::keys_of(rtc::input::Action::Drink), rtc::flask::state().charges,
                                                     rtc::flask::state().max));
        if (auto stamina = wcmods::cat().stamina()) ui.progress(L"Stamina", *stamina.value / std::max(1.0f, *stamina.max));
    }
} // namespace

class RealtimeCombat : public wcmods::ModBase
{
  public:
    auto on_unreal_init() -> void override
    {
        wcmods::log(L"loaded ({})", enabled() ? L"enabled" : L"switched off in its settings");
        rtc::input::load();
        rtc::lockon::load();
        rtc::attacks::load();
        rtc::enemies::load();
        rtc::foes::load();
        rtc::flask::load();
        wcmods::share("on", false);
        wcmods::share("state", rtc::code(gate().off));
        rtc::lockon::share();
        wcmods::every(0.25, update_gate);
        wcmods::every_frame(frame);

        // (The block's on the right mouse button, where the game also crouches. Dropping the game's crouch command
        // there doesn't stop it: the game keeps crouch going by the key itself (seen in game). It doesn't matter: while
        // guarding, your cat stands still in its guard pose, which covers the crouch.)

        // The page below has the settings there are so far.
        wcmods::settings_page(false);
        wcmods::menu_page(L"Real-time combat", page);
    }
};

WCMODS_MOD(RealtimeCombat)
