// Real-time combat: the game's turn-based fights swapped for real-time ones while you play a Sandbox mode cat.
// docs/plan.md, in this repository, has the plan and what each step adds.
//
// Step 1, the Sandbox gate: the mod is on while the Sandbox mode mod says a sandbox cat is being played (it shares
// "Sandbox.ready" with C++ mods, read here with wcmods::shared) and settings.json's "enabled" is on. While it's on,
// the roaming animals ignore your cat, so none chases it into a turn-based fight, and a pounce on one starts none
// either (seen in game: six pounces with them ignoring your cat started no fight; the first without did).
//
// What it shares (wcmods::share), for tests and other mods to read from Lua as
// WCModsCpp.mods["realtime-combat"].get(name): "on" (true or false) and "state" (rules.hpp's code(): "on",
// "disabled", "no-sandbox"...).
#include <WCMods/WCMods.hpp>

#include <format>
#include <string>

#include "rules.hpp"

namespace
{
    auto enabled() -> bool { return wcmods::settings()["enabled"].boolean(true); }

    auto set_enabled(bool on) -> void
    {
        wcmods::settings().set("enabled", on);
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
    // later, a look a second); turning off puts them back.
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
            if (on) wcmods::log(L"on: real-time combat, the animals ignore your cat");
            else wcmods::log(L"off ({}): the animals notice your cat again", rtc::why(off));
            g.on = on;
        }
        g.off = off;
    }
} // namespace

class RealtimeCombat : public wcmods::ModBase
{
  public:
    auto on_unreal_init() -> void override
    {
        wcmods::log(L"loaded ({})", enabled() ? L"enabled" : L"switched off in its settings");
        wcmods::share("on", false);
        wcmods::share("state", rtc::code(gate().off));
        wcmods::every(0.25, update_gate);

        // The page below has the one setting there is so far.
        wcmods::settings_page(false);
        wcmods::menu_page(L"Real-time combat", [](wcmods::Ui& ui) {
            ui.heading(L"Real-time combat");
            ui.note(L"Real-time fights in place of the game's turn-based ones, while you play a Sandbox mode cat. "
                    L"Work in progress: so far the animals leave your cat alone while it's on.");
            const Gate& g = gate();
            ui.field(L"Now", g.on ? std::wstring(L"On") : std::format(L"Off: {}", rtc::why(g.off)));
            const bool was = enabled();
            const bool now = ui.on_off(L"Real-time combat in Sandbox mode", was);
            if (now != was) set_enabled(now);
        });
    }
};

WCMODS_MOD(RealtimeCombat)
