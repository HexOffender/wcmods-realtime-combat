// attacks.hpp: step 3, your cat's attacks. F is a light attack (the fights' Quick Claw), G a heavy one (Strong Claw),
// F while running a lunge (Jump Attack) that carries your cat forward. Each plays the strike of its animation
// (rules.hpp's spec()), costs the game's own stamina, turns your cat to the animal it's locked on to, and at the
// moment it lands hits every animal in a cone in front (enemies.hpp). A light attack pressed in the last part of one
// follows it.
//
// Shared for tests: "attack" (the one playing: "light", "heavy", "lunge", or ""), "attacks" (how many made),
// "hits" (how many animals hit), "last_hit" (the last one's address), "refused" (attacks not made for want of
// stamina).
#pragma once

#include <WCMods/WCMods.hpp>

#include <optional>
#include <string>

#include "enemies.hpp"
#include "game.hpp"
#include "input.hpp"
#include "lockon.hpp"
#include "rules.hpp"
#include "stamina.hpp"

namespace rtc::attacks
{
    using wcmods::UObject;

    struct State
    {
        std::optional<Attack> now;
        UObject* montage{};
        double elapsed = 0;
        bool landed = false;
        std::optional<Attack> next;   // pressed during the last part of this one
        int made = 0, hits = 0, refused = 0;
        double damage_multiplier = 1;
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline auto load() -> void
    {
        std::lock_guard lock(wcmods::settings_lock());
        const double m = wcmods::settings()["cat_damage"].number(1);
        state().damage_multiplier = m > 0.05 && m < 20 ? m : 1;
    }

    inline auto busy() -> bool { return state().now.has_value(); }

    // Whether your cat's running: the game's Run held (Shift, a controller's right shoulder) and moving.
    inline auto running() -> bool
    {
        const bool run = wcmods::key_down(L"LeftShift") || wcmods::key_down(L"RightShift") || wcmods::key_down(L"Gamepad_RightShoulder");
        return run && game::speed(wcmods::player()) > 250;
    }

    inline auto start(Attack a) -> bool
    {
        State& s = state();
        UObject* me = wcmods::player();
        if (!me) return false;
        const AttackSpec& sp = spec(a);
        if (!stamina::spend(sp.stamina))
        {
            wcmods::share("refused", ++s.refused);
            return false;
        }
        if (UObject* target = lockon::locked()) wcmods::face(me, target);
        s.montage = game::play_from(me, sp.tag, sp.from, sp.rate);
        s.now = a;
        s.elapsed = 0;
        s.landed = false;
        s.next.reset();
        wcmods::share("attack", sp.name);
        wcmods::share("attacks", ++s.made);
        return true;
    }

    inline auto end() -> void
    {
        State& s = state();
        if (!s.now) return;
        if (s.montage && game::montage_on(wcmods::player(), s.montage)) game::stop_montage(wcmods::player(), s.montage, 0.25);
        s.now.reset();
        s.montage = nullptr;
        wcmods::share("attack", "");
    }

    // The moment it lands: every animal in the cone in front of your cat, within reach.
    inline auto land(Attack a) -> void
    {
        State& s = state();
        UObject* me = wcmods::player();
        auto at = game::at(me);
        auto yaw = game::facing(me);
        if (!at || !yaw) return;
        const AttackSpec& sp = spec(a);
        int32_t* level = wcmods::cat().level();
        const double damage = cat_damage(a, level ? *level : 1, s.damage_multiplier);
        for (const wcmods::Animal& animal : wcmods::animals(sp.reach + 200))
        {
            if (enemies::defeated(animal.actor)) continue;
            auto them = game::at(animal.actor);
            if (!them || !in_reach(*at, *yaw, *them, sp.reach, sp.cone)) continue;
            enemies::hit(animal.actor, damage, *at, sp.push);
            wcmods::share("hits", ++s.hits);
            wcmods::share("last_hit", static_cast<double>(game::id(animal.actor)));
        }
    }

    // Every frame while real-time combat is on.
    inline auto frame(double dt, const input::Pressed& p) -> void
    {
        State& s = state();
        std::optional<Attack> asked;
        if (p[input::Action::Lunge]) asked = Attack::Lunge;
        else if (p[input::Action::Heavy]) asked = Attack::Heavy;
        else if (p[input::Action::Light]) asked = running() ? Attack::Lunge : Attack::Light;

        if (!s.now)
        {
            if (asked) start(*asked);
            return;
        }
        const Attack a = *s.now;
        const AttackSpec& sp = spec(a);
        s.elapsed += dt;
        if (asked && *asked == Attack::Light && s.elapsed >= chain_from(sp)) s.next = Attack::Light;
        // a lunge carries your cat forward from a little before it lands to a little after (slid along the ground: the
        // game's movement doesn't move it while the animation plays)
        if (sp.lunge > 0 && s.elapsed >= hit_at(sp) - 0.25 && s.elapsed - dt < hit_at(sp) + 0.05)
            if (auto yaw = game::facing(wcmods::player())) game::slide(wcmods::player(), *yaw, sp.lunge * dt);
        if (!s.landed && s.elapsed >= hit_at(sp))
        {
            s.landed = true;
            land(a);
        }
        if (s.elapsed >= duration(sp))
        {
            auto follow = s.next;
            end();
            if (follow) start(*follow);
        }
    }
} // namespace rtc::attacks
