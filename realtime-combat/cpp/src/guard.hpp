// guard.hpp: step 7, blocking and parrying. Hold the block key (the right mouse button, where the game also crouches:
// the guard pose covers that, your cat standing still) and your cat crouches in a guard (a stand-in:
// its Stone Guard fight move held at its low crouch), turned to its lock-on target. An animal's hit landing from in
// front then does a quarter of its damage and the rest comes off stamina; with too little stamina the guard breaks
// (your cat reels a second, open). A guard begun just before the hit lands is a parry (a stand-in: Hissing Parry): no
// damage, and the animal reels 2 s, open to a riposte (enemies.hpp: your cat's next hit on it does three times). The
// rules are rules.hpp's GuardSpec and guard().
//
// No guard starts during an attack, a dodge, a drink or a hit reaction; an attack or a dodge pressed while guarding
// drops the guard. No stamina comes back while guarding.
//
// Shared for tests: "guarding" (true while it guards), "blocks", "guard_breaks", "parries", "guarded" (what the last
// hit on a guard did: "blocked", "guard-broken", "parried").
#pragma once

#include <WCMods/WCMods.hpp>

#include "attacks.hpp"
#include "dodge.hpp"
#include "enemies.hpp"
#include "flask.hpp"
#include "game.hpp"
#include "hurt.hpp"
#include "input.hpp"
#include "lockon.hpp"
#include "rules.hpp"
#include "stamina.hpp"

namespace rtc::blocking
{
    using wcmods::UObject;

    struct State
    {
        bool guarding = false;
        double held_for = 0;
        UObject* montage{};
        double broken_left = 0;   // seconds the guard can't come up (broken)
        UObject* parry_montage{};
        double parry_left = 0;
        double since_drop = 1e9;  // seconds since the guard last dropped
        int blocks = 0, breaks = 0, parries = 0;
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline auto spec() -> GuardSpec
    {
        GuardSpec g;
        // tests can widen the parry's window (reading the game from outside takes a while)
        const double w = wcmods::shared("RealtimeCombat.parry_window").number(0);
        if (w > 0 && w < 2) g.parry_window = w;
        return g;
    }

    inline auto busy() -> bool { return state().guarding || state().parry_left > 0; }

    inline auto drop() -> void
    {
        State& s = state();
        if (!s.guarding) return;
        UObject* me = wcmods::player();
        if (me && s.montage && game::montage_on(me, s.montage)) game::stop_montage(me, s.montage, 0.2);
        s.guarding = false;
        s.montage = nullptr;
        s.since_drop = 0;
        wcmods::share("guarding", false);
    }

    inline auto raise() -> void
    {
        State& s = state();
        UObject* me = wcmods::player();
        if (!me) return;
        const GuardSpec g = spec();
        if (UObject* target = lockon::locked()) game::turn_to(me, target);
        s.montage = game::play_from(me, g.guard_tag, g.guard_pose, 1.0);
        if (UObject* a = game::anim(me); a && s.montage)
            wcmods::call_reflected(a, STR("Montage_Pause"), [&](wcmods::Params& p) { p.set_nth(0, wcmods::Value::of(s.montage)); });
        s.guarding = true;
        // a guard raised again within half a second of dropping can't parry: tapping the key isn't a string of parries
        s.held_for = s.since_drop < 0.5 ? g.parry_window + 0.01 : 0;
        stamina::hold();   // none comes back while it guards
        wcmods::share("guarding", true);
    }

    // An animal's hit of `damage` landing on your cat: what the guard makes of it (rules.hpp's guard()), done here: the
    // chip, the stamina, a broken guard, a parry. Returns it, so the caller can make the animal reel on a parry.
    inline auto take(UObject* animal, double damage) -> Guarded
    {
        State& s = state();
        const GuardSpec g = spec();
        UObject* me = wcmods::player();
        auto at_me = game::at(me);
        auto them = game::at(animal);
        auto facing = game::facing(me);
        const bool in_front = at_me && them && facing && std::abs(turn(*facing, yaw_to(*at_me, *them))) <= g.front;
        auto stamina = wcmods::cat().stamina();
        const GuardResult r = guard(s.guarding, s.held_for, in_front, stamina ? *stamina.value : 0, damage, g);
        if (r.what != Guarded::Hit) wcmods::share("guarded", code(r.what));
        switch (r.what)
        {
        case Guarded::Hit: hurt::cat(r.health); break;
        case Guarded::Blocked:
            if (stamina) *stamina.value = static_cast<float>(std::max(0.0, *stamina.value - r.stamina));
            if (auto health = wcmods::cat().health())
            {
                if (*health.value - r.health <= 0) hurt::cat(r.health);   // the chip's enough: your cat falls
                else *health.value = static_cast<float>(*health.value - r.health);
            }
            wcmods::share("blocks", ++s.blocks);
            break;
        case Guarded::GuardBroken:
            if (stamina) *stamina.value = 0;
            drop();
            hurt::cat(r.health, g.break_for);
            s.broken_left = g.break_for;
            wcmods::share("guard_breaks", ++s.breaks);
            break;
        case Guarded::Parried:
            drop();
            s.parry_montage = game::play_from(me, g.parry_tag, g.parry_from, g.parry_rate);
            s.parry_left = parry_duration(g);
            enemies::open_to_riposte(animal, g.parry_reel, g.riposte);
            wcmods::share("parries", ++s.parries);
            break;
        }
        return r.what;
    }

    // Every frame while real-time combat is on. `held`: the block key's held; `free`: nothing else plays (an attack,
    // a dodge, a drink, a hit reaction).
    inline auto frame(double dt, bool held, bool free) -> void
    {
        State& s = state();
        UObject* me = wcmods::player();
        if (s.broken_left > 0) s.broken_left -= dt;
        s.since_drop += dt;
        if (s.parry_left > 0)
        {
            s.parry_left -= dt;
            if (s.parry_left <= 0 && me && s.parry_montage && game::montage_on(me, s.parry_montage))
                game::stop_montage(me, s.parry_montage, 0.2);
        }
        if (s.guarding)
        {
            s.held_for += dt;
            if (!held) drop();
            else if (UObject* target = lockon::locked()) game::turn_to(me, target);   // kept facing what it's locked on to
            return;
        }
        if (held && free && s.broken_left <= 0 && s.parry_left <= 0) raise();
    }

    inline auto clear() -> void
    {
        drop();
        state().broken_left = state().parry_left = 0;
    }
} // namespace rtc::blocking
