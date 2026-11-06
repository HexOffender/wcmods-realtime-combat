// flask.hpp: step 6, the flask. Z drinks: a stand-in animation (the game's sniff: it has no drinking one), and health
// back over the second half of it, 40% of your cat's most (rules.hpp's FlaskSpec). It has charges (3); a hit while
// drinking stops it, the charge spent, and no attack or dodge starts while it plays. It fills again when your cat
// rests: coming into the forest (real-time combat coming on), sleeping (the game's sleep animation playing), or going
// back to camp (any travel: a jump farther than running could make in a frame). It isn't an item in the inventory.
//
// Shared for tests: "flask" (charges left), "flask_max", "drinking" (true while one plays), "drinks" (how many
// made), "flask_refused" (presses with none left), "flask_refills".
#pragma once

#include <WCMods/WCMods.hpp>

#include <mutex>
#include <optional>

#include "game.hpp"
#include "input.hpp"
#include "rules.hpp"

namespace rtc::flask
{
    using wcmods::UObject;

    struct State
    {
        int charges = 3, max = 3;
        double heal = 0.4;
        bool drinking = false;
        double elapsed = 0;
        UObject* montage{};
        int drinks = 0, refused = 0, refills = 0;
        double sleep_check = 0;
        std::optional<Vec> last_at;
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline auto spec() -> FlaskSpec
    {
        FlaskSpec f;
        f.heal = state().heal;
        f.charges = state().max;
        return f;
    }

    inline auto share() -> void
    {
        wcmods::share("flask", state().charges);
        wcmods::share("flask_max", state().max);
    }

    inline auto load() -> void
    {
        State& s = state();
        {
            std::lock_guard lock(wcmods::settings_lock());
            const double n = wcmods::settings()["flask_charges"].number(3);
            s.max = n >= 0 && n <= 20 ? static_cast<int>(n) : 3;
            const double h = wcmods::settings()["flask_heal"].number(0.4);
            s.heal = h > 0 && h <= 1 ? h : 0.4;
        }
        s.charges = s.max;
        share();
    }

    inline auto busy() -> bool { return state().drinking; }

    // The flask full again.
    inline auto refill(const wchar_t* why) -> void
    {
        State& s = state();
        if (s.charges == s.max) return;
        s.charges = s.max;
        wcmods::share("flask_refills", ++s.refills);
        share();
        wcmods::log(L"the flask's full again ({})", why);
    }

    // A drink stopped (done, or a hit: the charge stays spent).
    inline auto end() -> void
    {
        State& s = state();
        if (!s.drinking) return;
        UObject* me = wcmods::player();
        if (me && s.montage && game::montage_on(me, s.montage)) game::stop_montage(me, s.montage, 0.2);
        s.drinking = false;
        s.montage = nullptr;
        wcmods::share("drinking", false);
    }

    inline auto start() -> bool
    {
        State& s = state();
        UObject* me = wcmods::player();
        if (!me) return false;
        if (s.charges <= 0)
        {
            wcmods::share("flask_refused", ++s.refused);
            return false;
        }
        const FlaskSpec f = spec();
        --s.charges;
        s.montage = game::play_from(me, f.tag, f.from, f.rate);
        s.drinking = true;
        s.elapsed = 0;
        wcmods::share("drinking", true);
        wcmods::share("drinks", ++s.drinks);
        share();
        return true;
    }

    // Every frame while real-time combat is on. `free` is false while an attack or a dodge plays (no drink starts then).
    inline auto frame(double dt, const input::Pressed& p, bool free) -> void
    {
        State& s = state();
        UObject* me = wcmods::player();
        auto at = game::at(me);
        // travel (back to camp, the map): the flask fills
        if (at && s.last_at && travelled(*s.last_at, *at)) refill(L"travelled");
        s.last_at = at;
        // sleeping (the game's sleep animation): the flask fills
        s.sleep_check -= dt;
        if (s.sleep_check <= 0 && !s.drinking)
        {
            s.sleep_check = 0.5;
            if (UObject* a = game::anim(me))
            {
                auto r = wcmods::call_reflected(a, STR("GetCurrentActiveMontage"));
                UObject* m = r ? r->get(L"ReturnValue").object : nullptr;
                if (m && wcmods::name(m).find(L"Sleep") != std::wstring::npos) refill(L"slept");
            }
        }

        if (!s.drinking)
        {
            if (p[input::Action::Drink] && free) start();
            return;
        }
        const FlaskSpec f = spec();
        const double before = s.elapsed;
        s.elapsed += dt;
        if (auto health = wcmods::cat().health())
        {
            const double add = healed_between(f, *health.max, before, s.elapsed);
            if (add > 0) *health.value = static_cast<float>(std::min<double>(*health.max, *health.value + add));
        }
        if (s.elapsed >= duration(f)) end();
    }
} // namespace rtc::flask
