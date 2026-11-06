// game.hpp: the few things the mod asks of the game, over wcmods' SDK, in rules.hpp's terms (rtc::Vec, rtc::Rot).
// Game thread only, as the SDK is.
#pragma once

#include <WCMods/WCMods.hpp>

#include <optional>

#include "rules.hpp"

namespace rtc::game
{
    using wcmods::UObject;

    inline auto vec(wcmods::Vec3 v) -> Vec { return {v.x, v.y, v.z}; }

    // Where an actor is (its middle: a cat's or an animal's capsule), or nullopt.
    inline auto at(UObject* actor) -> std::optional<Vec>
    {
        auto l = wcmods::location(actor);
        if (!l) return std::nullopt;
        return vec(*l);
    }

    // Whether an object is still there: not destroyed, not left behind by a level that went.
    inline auto staying(UObject* o) -> bool { return o && wcmods::detail::object_staying(o, nullptr); }

    // The controller's rotation, which the camera follows.
    inline auto control_rotation() -> std::optional<Rot>
    {
        auto r = wcmods::call_reflected(wcmods::controller(), STR("GetControlRotation"));
        if (!r) return std::nullopt;
        wcmods::Value v = r->get(L"ReturnValue");
        return Rot{wcmods::get(v, L"Pitch").number(), wcmods::get(v, L"Yaw").number()};
    }
    inline auto set_control_rotation(Rot r) -> bool
    {
        return wcmods::call_reflected(wcmods::controller(), STR("SetControlRotation"), [&](wcmods::Params& p) {
                   wcmods::set(p.nth(0), L"Pitch", r.pitch);
                   wcmods::set(p.nth(0), L"Yaw", r.yaw);
               }).has_value();
    }

    // Whether your cat can see an actor: a line from a little above your cat's middle to the actor's middle hits
    // nothing of the world before it (or hits the actor itself, or the ground right by it).
    inline auto can_see(UObject* actor) -> bool
    {
        auto me = at(wcmods::player());
        auto them = at(actor);
        if (!me || !them) return false;
        const wcmods::Vec3 from{me->x, me->y, me->z + 20}, to{them->x, them->y, them->z};
        auto hit = wcmods::trace(from, to);
        return !hit || distance(vec(*hit), *them) < 150;
    }

    // Where a place in the world is on screen, in the game's UI units (its UI is laid out for 3840 by 2160 and
    // scaled), or nullopt when it's behind the camera.
    inline auto on_screen(Vec place) -> std::optional<std::pair<double, double>>
    {
        auto r = wcmods::call_reflected(wcmods::object(STR("/Script/Engine.Default__GameplayStatics")), STR("ProjectWorldToScreen"),
                                        [&](wcmods::Params& p) {
                                            p.set_nth(0, wcmods::Value::of(wcmods::controller()));
                                            wcmods::Value w = p.nth(1);
                                            wcmods::set(w, L"X", place.x);
                                            wcmods::set(w, L"Y", place.y);
                                            wcmods::set(w, L"Z", place.z);
                                        });
        if (!r || !r->get(L"ReturnValue").boolean()) return std::nullopt;
        const double scale = wcmods::ui::viewport_scale();
        wcmods::Value s = r->nth(2);
        if (scale <= 0) return std::nullopt;
        return std::pair{wcmods::get(s, L"X").number() / scale, wcmods::get(s, L"Y").number() / scale};
    }

    // A number for an object, the same while it lasts: its address. Shared with tests, which compare it with Lua's
    // GetAddress().
    inline auto id(UObject* o) -> unsigned long long { return reinterpret_cast<unsigned long long>(o); }

    // Which way an actor faces (its yaw).
    inline auto facing(UObject* actor) -> std::optional<double>
    {
        auto r = wcmods::call_reflected(actor, STR("K2_GetActorRotation"));
        if (!r) return std::nullopt;
        return wcmods::get(r->get(L"ReturnValue"), L"Yaw").number();
    }

    // Turn an actor (only it: not its controller, so the camera's left to lock-on's easing) to face another.
    inline auto turn_to(UObject* actor, UObject* other) -> bool
    {
        auto from = at(actor);
        auto to = at(other);
        if (!from || !to) return false;
        const double yaw = yaw_to(*from, *to);
        return wcmods::call_reflected(actor, STR("K2_SetActorRotation"), [&](wcmods::Params& p) {
                   wcmods::set(p.nth(0), L"Yaw", yaw);
                   p.set_nth(1, wcmods::Value::of(false));   // bTeleportPhysics
               }).has_value();
    }

    // How fast an actor moves, along the ground (cm/s).
    inline auto speed(UObject* actor) -> double
    {
        auto r = wcmods::call_reflected(actor, STR("GetVelocity"));
        if (!r) return 0;
        wcmods::Value v = r->get(L"ReturnValue");
        return std::hypot(wcmods::get(v, L"X").number(), wcmods::get(v, L"Y").number());
    }

    // Throw a cat or an animal (a Character): `velocity` cm/s, along the ground and up, in place of what it had.
    inline auto launch(UObject* actor, Vec velocity) -> bool
    {
        return wcmods::call_reflected(actor, STR("LaunchCharacter"), [&](wcmods::Params& p) {
                   wcmods::Value v = p.nth(0);
                   wcmods::set(v, L"X", velocity.x);
                   wcmods::set(v, L"Y", velocity.y);
                   wcmods::set(v, L"Z", velocity.z);
                   p.set_nth(1, wcmods::Value::of(true));   // bXYOverride
                   p.set_nth(2, wcmods::Value::of(true));   // bZOverride
               }).has_value();
    }

    // Move a cat or an animal along the ground by `step` cm the way `yaw` points, as its own movement would: it stops
    // short of a wall (a line at its middle's height hits something within the step and a little more), and keeps
    // its height over the ground. For moves the game's own movement won't make: while one of the game's fight
    // animations plays on your cat, a LaunchCharacter does nothing (seen in game). False when it couldn't move.
    inline auto slide(UObject* actor, double yaw_degrees, double step) -> bool
    {
        auto now = at(actor);
        if (!now || step <= 0) return false;
        const double r = yaw_degrees / degrees_per_radian;
        const double dx = std::cos(r), dy = std::sin(r);
        const wcmods::Vec3 from{now->x, now->y, now->z};
        const wcmods::Vec3 ahead{now->x + dx * (step + 40), now->y + dy * (step + 40), now->z};
        if (wcmods::trace(from, ahead)) return false;   // a wall, a rock, an animal: no further
        auto ground_here = wcmods::ground(now->x, now->y, now->z + 50);
        auto ground_there = wcmods::ground(now->x + dx * step, now->y + dy * step, now->z + 80);
        if (!ground_here || !ground_there || std::abs(*ground_there - *ground_here) > 60) return false;   // a drop or a step too high
        const double height = now->z - *ground_here;
        return wcmods::teleport(actor, {now->x + dx * step, now->y + dy * step, *ground_there + height});
    }

    // The anim instance on an actor's mesh, which plays its montages.
    inline auto anim(UObject* actor) -> UObject*
    {
        auto r = wcmods::call_reflected(wcmods::get(actor, L"Mesh").object, STR("GetAnimInstance"));
        return r ? r->get(L"ReturnValue").object : nullptr;
    }

    // One of the game's animations by its tag (wcmods::play_montage), from `from` seconds into it, at `rate`: the
    // montage, or nullptr.
    inline auto play_from(UObject* actor, std::wstring_view tag, double from, double rate) -> UObject*
    {
        UObject* m = wcmods::play_montage(actor, tag, rate);
        UObject* a = anim(actor);
        if (!m || !a) return m;
        if (from > 0)
            wcmods::call_reflected(a, STR("Montage_SetPosition"), [&](wcmods::Params& p) {
                p.set_nth(0, wcmods::Value::of(m));
                p.set_nth(1, wcmods::Value::of(from));
            });
        return m;
    }

    // Stop a montage, blending out over `blend` seconds (nullptr: whatever plays).
    inline auto stop_montage(UObject* actor, UObject* montage, double blend = 0.25) -> void
    {
        UObject* a = anim(actor);
        if (!a) return;
        wcmods::call_reflected(a, STR("Montage_Stop"), [&](wcmods::Params& p) {
            p.set_nth(0, wcmods::Value::of(blend));
            p.set_nth(1, wcmods::Value::of(montage));
        });
    }

    // Whether this montage still plays on the actor.
    inline auto montage_on(UObject* actor, UObject* montage) -> bool
    {
        UObject* a = anim(actor);
        if (!a || !montage) return false;
        auto r = wcmods::call_reflected(a, STR("Montage_IsPlaying"), [&](wcmods::Params& p) { p.set_nth(0, wcmods::Value::of(montage)); });
        return r && r->get(L"ReturnValue").boolean();
    }
} // namespace rtc::game
