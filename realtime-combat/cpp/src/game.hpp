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
} // namespace rtc::game
