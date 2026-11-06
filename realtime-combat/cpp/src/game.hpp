// game.hpp: the few things the mod asks of the game, over wcmods' SDK, in rules.hpp's terms (rtc::Vec, rtc::Rot).
// Game thread only, as the SDK is.
#pragma once

#include <WCMods/WCMods.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

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

    // Why on_screen() last gave nothing: "call" (no camera to ask), "behind" (behind the camera), "scale" (the UI's
    // scale was 0), or "" when it gave a place.
    inline auto on_screen_why() -> std::string&
    {
        static std::string why;
        return why;
    }

    // Where a place in the world is on screen, in the game's UI units (its UI is laid out for 3840 by 2160 and
    // scaled), or nullopt when it's behind the camera: the engine's ProjectWorldToScreen, else worked out from the
    // camera's place, rotation and field of view (its manager's). The engine's said no for a place in plain view
    // while the view was on an old exploration camera, after one of the game's fights (seen in game).
    inline auto on_screen(Vec place) -> std::optional<std::pair<double, double>>
    {
        const double scale = wcmods::ui::viewport_scale();
        // the engine's own first: exact, when it answers
        auto engine = wcmods::call_reflected(wcmods::object(STR("/Script/Engine.Default__GameplayStatics")), STR("ProjectWorldToScreen"),
                                             [&](wcmods::Params& p) {
                                                 p.set_nth(0, wcmods::Value::of(wcmods::controller()));
                                                 wcmods::Value w = p.nth(1);
                                                 wcmods::set(w, L"X", place.x);
                                                 wcmods::set(w, L"Y", place.y);
                                                 wcmods::set(w, L"Z", place.z);
                                             });
        if (engine && engine->get(L"ReturnValue").boolean() && scale > 0)
        {
            on_screen_why().clear();
            wcmods::Value s = engine->nth(2);
            return std::pair{wcmods::get(s, L"X").number() / scale, wcmods::get(s, L"Y").number() / scale};
        }
        // else worked out from the camera: right across, near enough up and down (the game's camera has a vertical
        // offset this leaves out: about 8% of the screen's height lower than the engine's, seen in game)
        on_screen_why() = "call";
        UObject* cam = wcmods::get(wcmods::controller(), L"PlayerCameraManager").object;
        auto loc = wcmods::call_reflected(cam, STR("GetCameraLocation"));
        auto rot = wcmods::call_reflected(cam, STR("GetCameraRotation"));
        auto fov = wcmods::call_reflected(cam, STR("GetFOVAngle"));
        auto size = wcmods::ui::viewport_size();
        if (!loc || !rot || !fov || size.x <= 0 || size.y <= 0) return std::nullopt;
        wcmods::Value l = loc->get(L"ReturnValue"), r = rot->get(L"ReturnValue");
        const Vec eye{wcmods::get(l, L"X").number(), wcmods::get(l, L"Y").number(), wcmods::get(l, L"Z").number()};
        const double pitch = wcmods::get(r, L"Pitch").number() / degrees_per_radian, yaw = wcmods::get(r, L"Yaw").number() / degrees_per_radian;
        // the camera's axes: forward, right and up (no roll)
        const Vec f{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        const Vec rt{-std::sin(yaw), std::cos(yaw), 0};
        const Vec up{-std::sin(pitch) * std::cos(yaw), -std::sin(pitch) * std::sin(yaw), std::cos(pitch)};
        const Vec d{place.x - eye.x, place.y - eye.y, place.z - eye.z};
        auto dot = [](Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        const double depth = dot(d, f);
        on_screen_why() = "behind";
        if (depth < 1) return std::nullopt;
        // Unreal's field of view is across: half the width at depth 1 is tan(fov / 2)
        const double half = std::tan(fov->get(L"ReturnValue").number(90) / 2 / degrees_per_radian);
        if (half <= 0) return std::nullopt;
        const double nx = dot(d, rt) / (depth * half), ny = dot(d, up) / (depth * half) * (size.x / size.y);
        on_screen_why() = "scale";
        if (scale <= 0) return std::nullopt;
        on_screen_why().clear();
        return std::pair{(1 + nx) * size.x / 2 / scale, (1 - ny) * size.y / 2 / scale};
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

    // The way the player's asking your cat to move, as the camera sees it: forward and right, each -1 to 1, from the
    // game's own movement keys (W, A, S, D and the arrows) or a controller's left stick.
    inline auto move_input() -> std::pair<double, double>
    {
        auto down = [](const wchar_t* key) { return wcmods::key_down(key) ? 1.0 : 0.0; };
        double forward = down(L"W") + down(L"Up") - down(L"S") - down(L"Down");
        double right = down(L"D") + down(L"Right") - down(L"A") - down(L"Left");
        auto axis = [](const wchar_t* key) {
            auto r = wcmods::call_reflected(wcmods::controller(), STR("GetInputAnalogKeyState"),
                                            [&](wcmods::Params& p) { wcmods::set(p.nth(0), L"KeyName", key); });
            return r ? r->get(L"ReturnValue").number() : 0.0;
        };
        forward += axis(L"Gamepad_LeftY");
        right += axis(L"Gamepad_LeftX");
        return {std::clamp(forward, -1.0, 1.0), std::clamp(right, -1.0, 1.0)};
    }

    // Which way an actor's moving along the ground (its velocity's yaw), or nullopt when it's (nearly) still.
    inline auto moving_yaw(UObject* actor) -> std::optional<double>
    {
        auto r = wcmods::call_reflected(actor, STR("GetVelocity"));
        if (!r) return std::nullopt;
        wcmods::Value v = r->get(L"ReturnValue");
        const double x = wcmods::get(v, L"X").number(), y = wcmods::get(v, L"Y").number();
        if (std::hypot(x, y) < 100) return std::nullopt;
        return std::atan2(y, x) * degrees_per_radian;
    }

    // Set which way an actor faces (only it, not its controller).
    inline auto set_facing(UObject* actor, double yaw) -> bool
    {
        return wcmods::call_reflected(actor, STR("K2_SetActorRotation"), [&](wcmods::Params& p) {
                   wcmods::set(p.nth(0), L"Yaw", yaw);
                   p.set_nth(1, wcmods::Value::of(false));   // bTeleportPhysics
               }).has_value();
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

    // The roaming animals near your cat. wcmods::animals() walks every object in the game to find them, which costs
    // (several a second made the game stutter), so the mod does it at most twice a second, for those within 5,000
    // units, and everything in it shares that list: animals(radius) gives those within `radius` now (along the ground,
    // as wcmods::animals measures), nearest first.
    struct Near
    {
        std::vector<wcmods::Animal> all;
        double age = 1e9;   // seconds since the search
    };
    inline auto near_state() -> Near&
    {
        static Near n;
        return n;
    }
    inline auto tick(double dt) -> void { near_state().age += dt; }
    inline auto forget_animals() -> void { near_state() = {}; }
    inline auto animals(double radius) -> std::vector<wcmods::Animal>
    {
        Near& n = near_state();
        if (n.age > 0.5)
        {
            n.all = wcmods::animals(5000);
            n.age = 0;
        }
        std::vector<wcmods::Animal> out;
        auto me = at(wcmods::player());
        if (!me) return out;
        for (const wcmods::Animal& a : n.all)
        {
            if (!staying(a.actor)) continue;
            auto where = at(a.actor);
            if (!where) continue;
            const double d = flat_distance(*me, *where);
            if (d > radius) continue;
            wcmods::Animal copy = a;
            copy.distance = d;
            out.push_back(std::move(copy));
        }
        std::sort(out.begin(), out.end(), [](const wcmods::Animal& p, const wcmods::Animal& q) { return p.distance < q.distance; });
        return out;
    }

    // Move a cat or an animal along the ground by `step` cm the way `yaw` points, as its own movement would: its body
    // swept along (K2_SetActorLocation with a sweep), so it stops against whatever's in the way, a wall, a rock or an
    // animal, and its height kept over the ground. For moves the game's own movement won't make: while one of the
    // game's fight animations plays on your cat, a LaunchCharacter does nothing (seen in game). Moved without the
    // sweep (a teleport), your cat could end up inside an animal, and that starts the game's own fight even with the
    // animals ignoring your cat (seen in game). False when it didn't move.
    inline auto slide(UObject* actor, double yaw_degrees, double step) -> bool
    {
        auto now = at(actor);
        if (!now || step <= 0) return false;
        const double r = yaw_degrees / degrees_per_radian;
        const double dx = std::cos(r), dy = std::sin(r);
        auto ground_here = wcmods::ground(now->x, now->y, now->z + 50);
        auto ground_there = wcmods::ground(now->x + dx * step, now->y + dy * step, now->z + 80);
        if (!ground_here || !ground_there || std::abs(*ground_there - *ground_here) > 60) return false;   // a drop or a step too high
        const double height = now->z - *ground_here;
        auto put = [&](Vec to, bool sweep) {
            return wcmods::call_reflected(actor, STR("K2_SetActorLocation"), [&](wcmods::Params& p) {
                       wcmods::Value v = p.nth(0);
                       wcmods::set(v, L"X", to.x);
                       wcmods::set(v, L"Y", to.y);
                       wcmods::set(v, L"Z", to.z);
                       p.set_nth(1, wcmods::Value::of(sweep));   // bSweep
                       p.set_nth(3, wcmods::Value::of(false));   // bTeleport (after the hit result)
                   }).has_value();
        };
        // swept across a little above the higher of here and there (so a bump in the ground doesn't stop it); stopped
        // short, the rest tried again 30 cm up, as the game's movement steps over a stone or a tuft (a wall is taller;
        // and not when an animal's what stopped it: a rabbit's low enough to be stepped over, seen in game); then set
        // down on the ground where it got to
        const double across = std::max(now->z, *ground_there + height) + 8;
        if (!put({now->x + dx * step, now->y + dy * step, across}, true)) return false;
        auto after = at(actor);
        if (!after) return false;
        auto animal_ahead = [&] {
            for (const wcmods::Animal& a : animals(300))
                if (auto where = at(a.actor); where && a.actor != actor && flat_distance(*after, *where) < 90) return true;
            return false;
        };
        if (flat_distance(*now, *after) < step * 0.7 && !animal_ahead())
        {
            const double left = step - flat_distance(*now, *after);
            if (put({after->x, after->y, across + 30}, true))
                if (auto up = at(actor); up && up->z > after->z + 20)
                {
                    put({up->x + dx * left, up->y + dy * left, up->z}, true);
                    after = at(actor);
                    if (!after) return false;
                }
        }
        if (auto g = wcmods::ground(after->x, after->y, after->z + 50)) put({after->x, after->y, *g + height}, true);
        return flat_distance(*now, *after) > 0.5;
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
