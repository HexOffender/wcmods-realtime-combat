// lockon.hpp: step 2, lock-on. The lock key picks the animal nearest the middle of the view (rules.hpp's pick_target),
// the camera turns toward it every frame (lock_rotation), the mouse wheel moves to the next one to a side
// (next_target), and a marker sits on it on the HUD. It lets go on the key again, or when the animal's gone, too far
// or out of sight a while (keep_lock); with nothing to lock on to, the key puts the camera behind your cat, as the
// game's camera reset does.
//
// Shared for tests and other mods (WCModsCpp.mods["realtime-combat"].get(name)): "locked" (true or false), "target"
// (the animal's address, as Lua's GetAddress() gives it; 0 for none), "target_model" (its mesh's name, L"SK_Mice"),
// "released" (why the last lock ended: "key", "gone", "defeated", "too-far", "unseen", "off"), "marker" (whether the marker shows).
#pragma once

#include <WCMods/WCMods.hpp>

#include <format>
#include <string>

#include "enemies.hpp"
#include "game.hpp"
#include "input.hpp"
#include "rules.hpp"

namespace rtc::lockon
{
    using wcmods::UObject;

    struct State
    {
        UObject* target{};
        std::wstring model;
        double unseen_for = 0;     // seconds the target's been out of sight
        double sight_check = 0;    // seconds to the next look (a trace costs, so five times a second)
        bool seen = true;
        bool marker_shown = false;
        wcmods::Widget marker;     // on the HUD's CanvasExploration
        wcmods::Widget canvas;
        double hud_check = 0;      // seconds to the next look for the HUD (finding it again is a search)
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    // settings.json's lock-on settings, read as the game starts (load()).
    inline auto settings() -> LockSettings&
    {
        static LockSettings s;
        return s;
    }
    inline auto load() -> void
    {
        LockSettings s;
        {
            std::lock_guard lock(wcmods::settings_lock());
            const double reach = wcmods::settings()["lock_on_reach_m"].number(15);
            if (reach >= 3 && reach <= 60)
            {
                s.range = reach * 100;
                s.release_range = reach * 100 + 700;
            }
        }
        settings() = s;
    }

    inline auto locked() -> UObject* { return state().target; }

    inline auto share() -> void
    {
        const State& s = state();
        wcmods::share("locked", s.target != nullptr);
        wcmods::share("target", static_cast<double>(game::id(s.target)));
        wcmods::share("target_model", s.model);
    }

    inline auto show_marker(bool on) -> void
    {
        State& s = state();
        if (s.marker)
        {
            if (on) s.marker.show_passive();
            else s.marker.hide();
        }
        on = on && s.marker;
        if (on != s.marker_shown) wcmods::share("marker", on);
        s.marker_shown = on;
    }

    inline auto release(std::string_view why) -> void
    {
        State& s = state();
        if (!s.target) return;
        wcmods::log(L"lock-on let go ({})", wcmods::wide(why));
        s.target = nullptr;
        s.model.clear();
        show_marker(false);
        wcmods::share("released", why);
        share();
    }

    inline auto lock(UObject* animal) -> void
    {
        State& s = state();
        s.target = animal;
        s.model = wcmods::name(wcmods::get(animal, L"Mesh.SkeletalMesh").object);
        s.unseen_for = 0;
        s.sight_check = 0;
        s.seen = true;
        wcmods::log(L"locked on to {}", s.model);
        share();
    }

    // The animals that could be locked on to now, with whether your cat sees each.
    inline auto candidates(const LockSettings& ls) -> std::vector<std::pair<UObject*, Candidate>>
    {
        std::vector<std::pair<UObject*, Candidate>> out;
        for (const wcmods::Animal& a : wcmods::animals(ls.range + 100))
        {
            auto where = game::at(a.actor);
            if (!where || enemies::defeated(a.actor)) continue;
            out.push_back({a.actor, Candidate{game::id(a.actor), *where, game::can_see(a.actor)}});
        }
        return out;
    }

    inline auto only(const std::vector<std::pair<UObject*, Candidate>>& all) -> std::vector<Candidate>
    {
        std::vector<Candidate> c;
        for (auto& [actor, candidate] : all) c.push_back(candidate);
        return c;
    }

    // The lock key: let go, or pick one; with none to pick, the camera behind your cat.
    inline auto toggle() -> void
    {
        if (state().target)
        {
            release("key");
            return;
        }
        auto me = game::at(wcmods::player());
        auto view = game::control_rotation();
        if (!me || !view) return;
        const LockSettings ls = settings();
        auto all = candidates(ls);
        const int i = pick_target(*me, view->yaw, only(all), ls);
        // What it chose from, for tests: each one's address, distance, degrees right of the view, and whether seen.
        std::string seen;
        for (auto& [actor, c] : all)
            seen += std::format("{}:{:.0f}:{:.0f}:{} ", c.id, distance(*me, c.at), turn(view->yaw, yaw_to(*me, c.at)),
                                c.seen ? "seen" : "unseen");
        wcmods::share("last_pick", seen);
        if (i < 0)
        {
            wcmods::command(L"Player_Camera_Reset");
            return;
        }
        lock(all[static_cast<size_t>(i)].first);
    }

    inline auto next(int side) -> void
    {
        State& s = state();
        auto me = game::at(wcmods::player());
        auto now = game::at(s.target);
        if (!s.target || !me || !now) return;
        const LockSettings ls = settings();
        auto all = candidates(ls);
        const int i = next_target(*me, *now, game::id(s.target), side, only(all), ls);
        if (i >= 0) lock(all[static_cast<size_t>(i)].first);
    }

    // The marker on the target: a small diamond on the HUD, at the animal's middle on screen.
    inline auto place_marker(double dt) -> void
    {
        State& s = state();
        s.hud_check -= dt;
        if ((!s.canvas || !s.marker) && s.hud_check <= 0)
        {
            s.hud_check = 0.5;
            wcmods::Widget hud = wcmods::ui::hud();
            wcmods::Widget layer = hud ? hud.find(L"CanvasExploration") : wcmods::Widget{};
            if (layer && (layer != s.canvas || !s.marker))
            {
                s.canvas = layer;
                s.marker = wcmods::ui::rect(wcmods::UiColor::hex(L"#fff4d6"), 26, 26);
                wcmods::Slot slot = layer.add(s.marker);
                slot.anchors(0, 0, 0, 0);
                slot.alignment(0.5, 0.5);
                slot.size(26, 26);
                s.marker.set_angle(45);
                s.marker.set_opacity(0.9f);
            }
        }
        if (!s.marker) return;
        auto where = game::at(s.target);
        auto screen = where ? game::on_screen(*where) : std::nullopt;
        if (!screen)
        {
            show_marker(false);
            return;
        }
        s.marker.slot().position(screen->first, screen->second);
        show_marker(true);
    }

    // Every frame while real-time combat is on.
    inline auto frame(double dt, const input::Pressed& p) -> void
    {
        State& s = state();
        if (p[input::Action::Lock]) toggle();
        else if (s.target && p[input::Action::NextRight]) next(+1);
        else if (s.target && p[input::Action::NextLeft]) next(-1);
        if (!s.target) return;

        if (enemies::defeated(s.target))
        {
            release("defeated");
            return;
        }
        const bool gone = !game::staying(s.target);
        auto me = game::at(wcmods::player());
        auto them = gone ? std::nullopt : game::at(s.target);
        if (gone || !me || !them)
        {
            release("gone");
            return;
        }
        s.sight_check -= dt;
        if (s.sight_check <= 0)
        {
            s.sight_check = 0.2;
            s.seen = game::can_see(s.target);
        }
        // a long frame (a hitch, an area loading) counts as a short one: out of sight means out of sight in play
        s.unseen_for = s.seen ? 0 : s.unseen_for + std::min(dt, 0.25);
        const LockSettings ls = settings();
        const Release r = keep_lock(false, distance(*me, *them), s.unseen_for, ls);
        if (r != Release::Keep)
        {
            release(code(r));
            return;
        }
        if (auto now = game::control_rotation()) game::set_control_rotation(lock_rotation(*now, *me, *them, dt, ls));
        place_marker(dt);
    }
} // namespace rtc::lockon
