// fall.hpp: step 8, falling. Your cat at no health doesn't die the game's way (what the game does with a cat at 0 outside
// a fight isn't known): hurt.hpp catches it at 1 and this takes over. Your cat plays its "lose" animation (the game's,
// Animation.Combat.Lose.1), every fight ends (the animals go back to roaming, the lock lets go), and 2.6 s later it's
// back at its Clan's camp (the camp's checkpoint, wcmods::places(): the game's own Return to Camp does nothing sent
// from a mod), healed, its stamina and flask full. The put is held until the ground there has loaded: wcmods::travel
// doesn't wait for the forest, and a far place could otherwise leave your cat falling through the world.
//
// Shared for tests: "falling" (true from the fall until your cat stands at camp), "falls" (how many).
#pragma once

#include <WCMods/WCMods.hpp>

#include <cwctype>
#include <optional>
#include <string>

#include "attacks.hpp"
#include "dodge.hpp"
#include "flask.hpp"
#include "game.hpp"
#include "guard.hpp"
#include "lockon.hpp"
#include "rules.hpp"

namespace rtc::fall
{
    using wcmods::UObject;

    constexpr double lose_for = 2.6;     // seconds of the lose animation before camp
    constexpr double settle_max = 30;    // seconds the put's held for the ground to load, at most

    struct State
    {
        bool falling = false;
        double t = 0;
        bool put = false;
        std::optional<Vec> camp;
        std::optional<Vec> from;   // where your cat fell
        int falls = 0;
        std::function<void()> end_fights;   // main.cpp's: every animal fighting let go
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline auto busy() -> bool { return state().falling; }

    // Your Clan's camp checkpoint ("Gameplay.Checkpoints.Shadowclan.0" for a ShadowClan cat: Sandbox mode shares the
    // Clan), else the nearest place your cat has unlocked.
    inline auto camp() -> std::optional<Vec>
    {
        std::wstring clan = wcmods::shared("Sandbox.clan").wstring();
        for (size_t i = 1; i < clan.size(); ++i) clan[i] = static_cast<wchar_t>(std::towlower(clan[i]));
        const std::wstring want = L"Gameplay.Checkpoints." + clan + L".0";
        auto me = game::at(wcmods::player());
        std::optional<Vec> nearest;
        double best = 1e18;
        for (const wcmods::Place& p : wcmods::places())
        {
            const Vec at = game::vec(p.at);
            if (!clan.empty() && p.tag == want) return at;
            if (p.unlocked && me && flat_distance(*me, at) < best)
            {
                best = flat_distance(*me, at);
                nearest = at;
            }
        }
        return nearest;
    }

    // Your cat's down: the lose animation, every fight ended, and camp in a moment.
    inline auto begin() -> void
    {
        State& s = state();
        if (s.falling) return;
        s.falling = true;
        s.t = 0;
        s.put = false;
        s.camp.reset();
        s.from = game::at(wcmods::player());
        attacks::end();
        dodge::end();
        flask::end();
        blocking::clear();
        lockon::release("fell");
        if (s.end_fights) s.end_fights();
        if (UObject* me = wcmods::player()) wcmods::play_montage(me, L"Animation.Combat.Lose.1");
        wcmods::share("falling", true);
        wcmods::share("falls", ++s.falls);
        wcmods::log(L"your cat fell; back to camp");
    }

    inline auto stand(UObject* me) -> void
    {
        State& s = state();
        s.falling = false;
        wcmods::share("falling", false);
        if (auto health = wcmods::cat().health()) *health.value = *health.max;
        if (auto stamina = wcmods::cat().stamina()) *stamina.value = *stamina.max;
        flask::refill(L"back at camp");
        (void)me;
    }

    // Every frame while real-time combat is on (and while falling, even as the gate wavers at the put).
    inline auto frame(double dt) -> void
    {
        State& s = state();
        if (!s.falling) return;
        UObject* me = wcmods::player();
        if (!me) return;   // a loading screen: wait
        s.t += dt;
        if (s.t < lose_for) return;
        if (!s.put)
        {
            s.put = true;
            s.camp = camp();
            if (auto health = wcmods::cat().health()) *health.value = *health.max;
            game::stop_montage(me, nullptr, 0.2);
            if (!s.camp)
            {
                stand(me);   // nowhere to go: up where it fell
                return;
            }
            wcmods::teleport(me, {s.camp->x, s.camp->y, s.camp->z + 200});
            return;
        }
        // held over the camp until the ground's there, then set down on it
        if (auto g = wcmods::ground(s.camp->x, s.camp->y, s.camp->z + 400); g && std::abs(*g - s.camp->z) < 400)
        {
            wcmods::teleport(me, {s.camp->x, s.camp->y, *g + 60});
            stand(me);
            return;
        }
        if (s.t > lose_for + settle_max)
        {
            // the ground never came: back where your cat fell rather than dropped into nothing
            if (s.from) wcmods::teleport(me, {s.from->x, s.from->y, s.from->z + 30});
            stand(me);
            return;
        }
        wcmods::teleport(me, {s.camp->x, s.camp->y, s.camp->z + 200});
    }
} // namespace rtc::fall
