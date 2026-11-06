// stamina.hpp: your cat's stamina as attacks and dodges use it. It's the game's own (Cat::stamina(), 100 on a cat, the
// one running uses), which the game fills again at 20 a second: an action spends it (rules.hpp's can_spend/spend),
// and none comes back while one plays or for a moment after (rules.hpp's stamina_pause), so a few attacks in a row
// run it down.
#pragma once

#include <WCMods/WCMods.hpp>

#include "rules.hpp"

namespace rtc::stamina
{
    struct State
    {
        double pause = 0;    // seconds more that none comes back, once nothing's playing
        float held = -1;     // the most it may be while it's held (-1: not held)
        wcmods::Resource resource;   // your cat's, found as it's spent (it's the same while it's held)
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    // Spend `cost` for an action: false, with nothing spent, when there's too little.
    inline auto spend(double cost) -> bool
    {
        auto r = wcmods::cat().stamina();
        if (!r || !can_spend(*r.value, cost)) return false;
        *r.value = static_cast<float>(rtc::spend(*r.value, cost));
        state().resource = r;
        state().held = *r.value;
        state().pause = stamina_pause;
        return true;
    }

    // Every frame while real-time combat is on: while an action plays (`busy`) and for the pause after, it doesn't
    // come back (it can still go down, as running spends it).
    inline auto frame(double dt, bool busy) -> void
    {
        State& s = state();
        if (s.held < 0) return;
        const wcmods::Resource& r = s.resource;
        if (!r || !wcmods::player())
        {
            s.held = -1;
            return;
        }
        if (busy) s.pause = stamina_pause;
        else s.pause -= dt;
        if (s.pause <= 0)
        {
            s.held = -1;
            return;
        }
        if (*r.value > s.held) *r.value = s.held;
        else s.held = *r.value;
    }

    inline auto clear() -> void { state().held = -1; }
} // namespace rtc::stamina
