// attacks.hpp: step 3, your cat's attacks. F is a light attack (the fights' Quick Claw), G a heavy one (Strong Claw),
// F while running a lunge (Jump Attack) that carries your cat forward. Each plays the strike of its animation
// (rules.hpp's spec()), costs the game's own stamina, turns your cat to the animal it's locked on to, and at the
// moment it lands hits one animal in front that your cat can see: the one it's locked on to, else the nearest
// (enemies.hpp). The heavy attack is a wide swing and hits every animal in the cone. A light attack pressed once one
// has landed follows it.
//
// Shared for tests: "attack" (the one playing: "light", "heavy", "lunge", or ""), "attacks" (how many made),
// "hits" (how many animals hit), "last_hit" (the last one's address), "refused" (attacks not made for want of
// stamina).
#pragma once

#include <WCMods/WCMods.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

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
        double stepped = 0;           // cm this strike has stepped in (AttackSpec::step)
        std::optional<Attack> next;   // pressed during the last part of this one
        int made = 0, hits = 0, refused = 0;
        double damage_multiplier = 1;
        // foes.hpp's, set as the mod starts: an attack begun (the animals in front may guard or hop aside), and the
        // part of a hit's damage an animal takes (its guard, its hop; 1 for the whole hit, 0 for none)
        std::function<void(Attack)> on_start;
        std::function<double(UObject*, Attack, Vec)> on_land;
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
        if (UObject* target = lockon::locked()) game::turn_to(me, target);
        s.montage = game::play_from(me, sp.tag, sp.from, sp.rate);
        if (s.on_start) s.on_start(a);
        s.now = a;
        s.elapsed = 0;
        s.landed = false;
        s.stepped = 0;
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

    // The moment it lands: on one animal (or Clan life cat) in reach in front of your cat, the one it's locked on to or
    // else the nearest; a wide swing (the heavy attack) on every one in the cone.
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
        std::vector<UObject*> candidates;   // in reach, nearest first (game::fighters sorts by distance)
        std::optional<size_t> locked;
        for (const wcmods::Animal& animal : game::fighters(sp.reach + 200))
        {
            if (enemies::defeated(animal.actor)) continue;
            auto them = game::at(animal.actor);
            if (!them || !in_reach(*at, *yaw, *them, sp.reach, sp.cone) || !game::can_see(animal.actor)) continue;
            if (animal.actor == lockon::locked()) locked = candidates.size();
            candidates.push_back(animal.actor);
        }
        for (size_t i : struck(sp, candidates.size(), locked))
        {
            UObject* actor = candidates[i];
            const double part = s.on_land ? s.on_land(actor, a, *at) : 1;
            if (part <= 0) continue;   // it hopped out of reach, or its guard broke (it reels, open)
            // through its guard: a part of the damage, and no flinch or push
            enemies::hit(actor, damage * part, *at, part < 1 ? 0 : sp.push, part >= 1);
            enemies::count_yours(actor);
            wcmods::share("hits", ++s.hits);
            wcmods::share("last_hit", static_cast<double>(game::id(actor)));
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
        UObject* me = wcmods::player();
        UObject* target = lockon::locked();
        // until it lands, a strike keeps facing what it's locked on to (an animal pacing round your cat walked out of
        // the cone during the wind-up, and the blow missed)
        if (target && !s.landed) game::turn_to(me, target);
        // a lunge carries your cat forward from a little before it lands to a little after (slid along the ground: the
        // game's movement doesn't move it while the animation plays)
        if (sp.lunge > 0 && s.elapsed >= hit_at(sp) - 0.25 && s.elapsed - dt < hit_at(sp) + 0.05)
            if (auto yaw = game::facing(me)) game::slide(me, *yaw, sp.lunge * dt);
        // a strike steps in over the last 0.3 s of its wind-up: toward what it's locked on to, as far as it needs (up
        // to `step`) to bring it in reach (the animals pace just out of reach, and most blows fell short), or a short
        // step forward with nothing locked
        if (sp.step > 0 && !s.landed && s.elapsed >= hit_at(sp) - 0.3)
        {
            double gap = sp.step * 0.5 - s.stepped;
            if (target)
            {
                auto at = game::at(me), them = game::at(target);
                gap = at && them ? flat_distance(*at, *them) - (sp.reach - 40) : 0;
            }
            const double move = std::min({gap, sp.step - s.stepped, 500 * dt});
            if (move > 0)
                if (auto yaw = game::facing(me))
                    if (game::slide(me, *yaw, move)) s.stepped += move;
        }
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
