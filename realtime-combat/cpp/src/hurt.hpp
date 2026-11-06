// hurt.hpp: your cat taking a hit (step 5's animals' attacks, and the chip through a guard): health off, whatever it
// was doing cut short (an attack, a dodge past its safe moment, a drink, a guard), and a short hit reaction: the game's
// own, cut after 0.45 s. A guard broken reels longer. A hit that would leave no health leaves 1, and your cat falls
// (fall.hpp, through on_fall: the game never sees a cat at 0 outside its fights).
//
// Shared for tests: "cat_hits" (how many hits your cat took).
#pragma once

#include <WCMods/WCMods.hpp>

#include <functional>

#include "attacks.hpp"
#include "dodge.hpp"
#include "flask.hpp"
#include "game.hpp"

namespace rtc::hurt
{
    using wcmods::UObject;

    struct State
    {
        double left = 0;       // the hit reaction's seconds before it's cut short
        UObject* montage{};
        int hits = 0;
        std::function<void()> on_fall;   // main.cpp's: fall::begin
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    // Whether your cat's reeling from a hit (it starts nothing until it's done).
    inline auto reeling() -> bool { return state().left > 0; }

    // Health off (`damage`, not below 1), what it was doing cut short, and its hit reaction for `reel` seconds.
    inline auto cat(double damage, double reel = 0.45) -> void
    {
        State& s = state();
        UObject* me = wcmods::player();
        auto health = wcmods::cat().health();
        if (!me || !health) return;
        const bool down = damage > 0 && *health.value - damage <= 0;
        if (damage > 0) *health.value = static_cast<float>(std::max(1.0, *health.value - damage));
        if (down && s.on_fall)
        {
            wcmods::share("cat_hits", ++s.hits);
            s.on_fall();
            return;
        }
        attacks::end();
        dodge::end();   // (past its safe moment: the hit cuts the hop short)
        flask::end();   // a drink stops, the charge spent
        s.montage = wcmods::play_montage(me, L"Animation.Combat.Hit.1.Neutral", reel > 0.6 ? 0.9 : 1.2);
        s.left = reel;
        wcmods::share("cat_hits", ++s.hits);
    }

    // Every frame while real-time combat is on: the hit reaction cut short.
    inline auto frame(double dt) -> void
    {
        State& s = state();
        if (s.left <= 0) return;
        s.left -= dt;
        UObject* me = wcmods::player();
        if (s.left <= 0 && me && s.montage && game::montage_on(me, s.montage)) game::stop_montage(me, s.montage, 0.2);
    }

    inline auto clear() -> void { state().left = 0; }
} // namespace rtc::hurt
