// dodge.hpp: step 4, the dodge. Left Alt hops your cat the way the player's moving (its keys, or failing those the way
// your cat's going; straight back, standing still):
// the game's side hop (Animation.Combat.Dodge.Left/Right) to that side, or, forward and back, your cat turned so its
// right hop goes that way, and slid about 3 m along the ground while the hop's in the air (rules.hpp's DodgeSpec). For
// its first 0.4 s nothing hurts your cat (safe(), which step 5's enemies ask). It costs 15 stamina, and can cut an
// attack short once the attack has landed.
//
// Shared for tests: "dodge" (true while one plays), "dodges" (how many made), "dodge_safe" (true while nothing hurts
// your cat), "dodge_safe_starts" (how many safe moments have begun), "dodge_way" (the yaw it went), "dodge_refused"
// (dodges not made for want of stamina).
#pragma once

#include <WCMods/WCMods.hpp>

#include "attacks.hpp"
#include "game.hpp"
#include "input.hpp"
#include "rules.hpp"
#include "stamina.hpp"

namespace rtc::dodge
{
    using wcmods::UObject;

    struct State
    {
        bool on = false;
        double elapsed = 0;
        double way = 0;          // the yaw it goes
        UObject* montage{};
        bool was_safe = false;
        int made = 0, refused = 0, safe_starts = 0;
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline auto busy() -> bool { return state().on; }
    // Whether nothing hurts your cat now: the first part of a dodge.
    inline auto safe() -> bool { return state().on && safe_at(DodgeSpec{}, state().elapsed); }

    inline auto end() -> void
    {
        State& s = state();
        if (!s.on) return;
        UObject* me = wcmods::player();
        if (me && s.montage && game::montage_on(me, s.montage)) game::stop_montage(me, s.montage, 0.2);
        // a forward or back dodge turned your cat side-on: locked on, it faces its target again
        if (me && lockon::locked()) game::turn_to(me, lockon::locked());
        s.on = false;
        s.montage = nullptr;
        wcmods::share("dodge", false);
        if (s.was_safe) wcmods::share("dodge_safe", false);
        s.was_safe = false;
    }

    inline auto start(std::optional<double> way_from_camera) -> bool
    {
        State& s = state();
        UObject* me = wcmods::player();
        auto facing = game::facing(me);
        auto view = game::shown_yaw();   // the game walks your cat relative to the view shown
        if (!view) view = game::view_yaw();
        if (!me || !facing || !view) return false;
        const DodgeSpec d;
        if (!stamina::spend(d.stamina))
        {
            wcmods::share("dodge_refused", ++s.refused);
            return false;
        }
        if (way_from_camera) s.way = *view + *way_from_camera;
        else
        {
            auto [forward, right] = game::move_input();
            s.way = dodge_yaw(*view, *facing, forward, right);
            // no movement key it knows held, but your cat's moving (keys moved by a remapper, say): that way
            if (std::hypot(forward, right) < 0.2)
                if (auto moving = game::moving_yaw(me)) s.way = *moving;
        }
        const DodgePose pose = dodge_pose(*facing, s.way);
        if (std::abs(turn(pose.cat_yaw, *facing)) > 0.5) game::set_facing(me, pose.cat_yaw);
        s.montage = game::play_from(me, pose.right ? L"Animation.Combat.Dodge.Right" : L"Animation.Combat.Dodge.Left", d.from, d.rate);
        s.on = true;
        s.elapsed = 0;
        s.was_safe = true;
        wcmods::share("dodge_safe", true);
        wcmods::share("dodge_safe_starts", ++s.safe_starts);
        wcmods::share("dodge", true);
        wcmods::share("dodges", ++s.made);
        wcmods::share("dodge_way", s.way);
        return true;
    }

    // Every frame while real-time combat is on.
    inline auto frame(double dt, const input::Pressed& p) -> void
    {
        State& s = state();
        if (!s.on && p[input::Action::Dodge])
        {
            // out of an attack once it's landed; not in its wind-up
            if (attacks::busy())
            {
                auto& a = attacks::state();
                if (!a.now || !can_dodge_out_of(spec(*a.now), a.elapsed)) return;
                attacks::end();
            }
            start(p.dodge_way);
            return;
        }
        if (!s.on) return;
        const DodgeSpec d;
        const double before = s.elapsed;
        s.elapsed += dt;
        // the slide: the part of this frame inside the hop's window
        const double from = std::max(before, slide_start(d)), to = std::min(s.elapsed, slide_end(d));
        if (to > from) game::slide(wcmods::player(), s.way, slide_speed(d) * (to - from));
        const bool now_safe = safe_at(d, s.elapsed);
        if (now_safe != s.was_safe) wcmods::share("dodge_safe", now_safe);
        s.was_safe = now_safe;
        if (s.elapsed >= duration(d)) end();
    }
} // namespace rtc::dodge
