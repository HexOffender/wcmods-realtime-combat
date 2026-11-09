// hud.hpp: step 8, the HUD. On the game's own HUD (its CanvasExploration layer, so it goes when the game's menus and
// fights hide the HUD):
//   - a health bar over every animal your cat's hit, at its head on screen; the one locked on to gets a bigger bar
//     with its name and level over it, and its posture under it (gold: how near its guard is to breaking);
//   - the flask's drinks under the game's health bar, one pip each, lit for those left. (Your cat's stamina is the
//     game's own bar at the bottom of the screen.)
// Sizes are the game's UI units (laid out for 3840 by 2160). Everything's made once and moved each frame; the HUD's
// looked for again twice a second (the game makes a new one after a fight or a new area).
//
// Shared for tests: "bars" (how many animal bars show), "target_bar" (true while the target's big bar shows), "mark" (the
// address of the animal the red mark shows over, its blow about to land; nil while none), "warning" (one the yellow mark
// shows over, winding up; nil while none).
#pragma once

#include <WCMods/WCMods.hpp>

#include <format>
#include <map>
#include <string>
#include <vector>

#include "allies.hpp"
#include "enemies.hpp"
#include "flask.hpp"
#include "foes.hpp"
#include "game.hpp"
#include "lockon.hpp"

namespace rtc::hud
{
    using wcmods::UObject;
    using wcmods::Widget;

    struct Bar
    {
        Widget bar, name, posture;
        Widget warn;   // a mark over it while it winds up an attack: yellow, then red for the last moment (its blow about
                       // to land): dodge or guard now
        bool warn_hot = true;   // the mark's colour now: red (true) or yellow
        bool big = false;
    };
    inline auto warn_color(bool hot) -> wcmods::UiColor { return wcmods::UiColor::hex(hot ? L"#ff4b3a" : L"#ffd84a"); }
    struct State
    {
        Widget canvas;
        double look = 0;
        Widget flask;                 // the flask's label
        std::vector<Widget> pips;     // a pip a drink
        int pips_lit = -1;
        std::map<UObject*, Bar> bars;
        std::map<UObject*, Bar> ally_bars;   // your party's (allies.hpp), green, with their names
        int shown = -1;
        bool target_shown = false;
        UObject* marked{};   // the animal the red mark showed over last frame (shared as "mark")
        UObject* warned{};   // one the yellow mark showed over (shared as "warning")
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    // "SK_Rabbit" -> "Rabbit", "SK_Mice" -> "Mouse".
    inline auto display_name(std::wstring model) -> std::wstring
    {
        if (model.rfind(L"SK_", 0) == 0) model = model.substr(3);
        if (model == L"Mice") return L"Mouse";
        return model.empty() ? std::wstring(L"Animal") : model;
    }

    inline auto place(Widget& w, double x, double y, double ax, double ay) -> void
    {
        wcmods::Slot slot = w.slot();
        slot.anchors(0, 0, 0, 0);
        slot.alignment(ax, ay);
        slot.position(x, y);
    }

    inline auto make_bar(bool big) -> Bar
    {
        State& s = state();
        Bar b;
        b.big = big;
        b.bar = wcmods::ui::progress(1.0, wcmods::UiColor::hex(L"#c0392b"));
        s.canvas.add(b.bar).size(big ? 300 : 170, big ? 22 : 13);
        b.bar.set_opacity(0.92f);
        if (big)
        {
            b.name = wcmods::ui::text(L"", {.size = 30, .color = wcmods::UiColor::hex(L"#f4e3b5")});
            s.canvas.add(b.name).auto_size(true);
            b.posture = wcmods::ui::progress(0.0, wcmods::UiColor::hex(L"#e8c547"));
            s.canvas.add(b.posture).size(300, 8);
            b.posture.set_opacity(0.92f);
        }
        b.warn = wcmods::ui::text(L"!", {.size = big ? 46.0f : 32.0f, .color = warn_color(true)});
        s.canvas.add(b.warn).auto_size(true);
        b.warn.hide();
        return b;
    }

    inline auto make_ally_bar() -> Bar
    {
        State& s = state();
        Bar b;
        b.bar = wcmods::ui::progress(1.0, wcmods::UiColor::hex(L"#4f9d5d"));
        s.canvas.add(b.bar).size(150, 11);
        b.bar.set_opacity(0.92f);
        b.name = wcmods::ui::text(L"", {.size = 22, .color = wcmods::UiColor::hex(L"#d8efd0")});
        s.canvas.add(b.name).auto_size(true);
        return b;
    }

    inline auto remove_bar(Bar& b) -> void
    {
        if (b.bar) b.bar.remove();
        if (b.name) b.name.remove();
        if (b.posture) b.posture.remove();
        if (b.warn) b.warn.remove();
    }

    inline auto clear_bars() -> void
    {
        for (auto& [animal, b] : state().bars) remove_bar(b);
        state().bars.clear();
        for (auto& [cat, b] : state().ally_bars) remove_bar(b);
        state().ally_bars.clear();
    }

    // The flask's pips: one a drink, made again when the most changes.
    inline auto make_pips(int count) -> void
    {
        State& s = state();
        for (Widget& p : s.pips) p.remove();
        s.pips.clear();
        s.pips_lit = -1;
        for (int i = 0; i < count; ++i)
        {
            Widget p = wcmods::ui::rect(wcmods::UiColor::hex(L"#e9a43a"), 24, 24);
            s.canvas.add(p).size(24, 24);
            place(p, 496 + i * 34, 312, 0, 0);
            s.pips.push_back(p);
        }
    }

    // The HUD's layer, looked for again twice a second; a new one gets everything made again.
    inline auto find_hud(double dt) -> bool
    {
        State& s = state();
        s.look -= dt;
        if (s.look <= 0 || !s.canvas)
        {
            s.look = 0.5;
            Widget hud = wcmods::ui::hud();
            Widget layer = hud ? hud.find(L"CanvasExploration") : Widget{};
            wcmods::share("hud", static_cast<bool>(layer));
            if (layer && layer != s.canvas)
            {
                clear_bars();
                for (Widget& p : s.pips) p.remove();
                s.pips.clear();
                if (s.flask) s.flask.remove();
                s.canvas = layer;
                s.flask = wcmods::ui::text(L"Flask", {.size = 28, .color = wcmods::UiColor::hex(L"#f4e3b5")});
                s.canvas.add(s.flask).auto_size(true);
                place(s.flask, 386, 306, 0, 0);
                make_pips(flask::state().max);
            }
        }
        return static_cast<bool>(s.canvas);
    }

    // Every frame while real-time combat is on.
    inline auto frame(double dt) -> void
    {
        State& s = state();
        if (!find_hud(dt)) return;

        if (s.flask)
        {
            const auto& f = flask::state();
            // pips, not characters: the game's font has no diamond or circle (they showed as question marks)
            if (static_cast<int>(s.pips.size()) != f.max) make_pips(f.max);
            if (f.charges != s.pips_lit)
            {
                for (int i = 0; i < static_cast<int>(s.pips.size()); ++i) s.pips[i].set_opacity(i < f.charges ? 0.95f : 0.22f);
                s.pips_lit = f.charges;
                wcmods::share("flask_pips", f.charges);
            }
            s.flask.show_passive();
            for (Widget& p : s.pips) p.show_passive();
        }

        // a bar over each animal hit (or locked on to: its record made, whole, so its bar shows): made as it's first
        // wanted, gone with it
        UObject* target = lockon::locked();
        if (target && !enemies::known(target) && !enemies::defeated(target)) enemies::of(target);
        int shown = 0;
        bool target_shown = false;
        UObject* marked = nullptr;   // the animal with the red mark
        UObject* warned = nullptr;   // one with the yellow
        std::erase_if(s.bars, [](auto& e) {
            const bool keep = game::staying(e.first) && enemies::known(e.first) && !enemies::defeated(e.first);
            if (!keep) remove_bar(e.second);
            return !keep;
        });
        for (auto& [animal, e] : enemies::state().all)
        {
            // a bar over one your cat's hit, the one it's locked on to, and one winding up an attack at it (its mark
            // shows whether or not it's locked on to: the one that matters most isn't, coming in from the side)
            const bool attacking_you = foes::winding_up(animal) || foes::striking_soon(animal);
            if (e.defeated || (e.hits == 0 && animal != target && !attacking_you) || !game::staying(animal))
            {
                if (auto it = s.bars.find(animal); it != s.bars.end())   // (was locked on to, or attacking: hidden now)
                {
                    it->second.bar.hide();
                    if (it->second.name) it->second.name.hide();
                    if (it->second.posture) it->second.posture.hide();
                    if (it->second.warn) it->second.warn.hide();
                }
                continue;
            }
            const bool big = animal == target;
            auto it = s.bars.find(animal);
            if (it != s.bars.end() && it->second.big != big)
            {
                remove_bar(it->second);
                s.bars.erase(it);
                it = s.bars.end();
            }
            if (it == s.bars.end()) it = s.bars.emplace(animal, make_bar(big)).first;
            Bar& b = it->second;
            auto at = game::at(animal);
            auto screen = at ? game::on_screen({at->x, at->y, at->z + 30}) : std::nullopt;
            if (!screen)
            {
                b.bar.hide();
                if (b.name) b.name.hide();
                if (b.posture) b.posture.hide();
                if (b.warn) b.warn.hide();
                continue;
            }
            b.bar.set_value(e.max > 0 ? e.health / e.max : 0);
            place(b.bar, screen->first, screen->second, 0.5, 1.0);
            b.bar.show_passive();
            ++shown;
            // winding up an attack: the mark over it, yellow (don't start an attack now), red for the last moment when its
            // blow is about to land (dodge or guard now)
            if (b.warn)
            {
                const bool hot = foes::striking_soon(animal);
                if (hot || foes::winding_up(animal))
                {
                    if (hot != b.warn_hot)
                    {
                        b.warn.set_color(warn_color(hot));
                        b.warn_hot = hot;
                    }
                    place(b.warn, screen->first, screen->second - (big ? 62 : 16), 0.5, 1.0);
                    b.warn.show_passive();
                    if (hot) marked = animal;
                    else if (!warned) warned = animal;
                }
                else b.warn.hide();
            }
            if (b.name)
            {
                const std::wstring label = std::format(L"{}  ·  level {}", e.display.empty() ? display_name(e.model) : e.display, e.level);
                if (b.name.text() != label) b.name.set_text(label);
                place(b.name, screen->first, screen->second - 26, 0.5, 1.0);
                b.name.show_passive();
                target_shown = true;
            }
            if (b.posture)
            {
                // under its health: how near its guard is to breaking (none for an animal not fighting)
                const double p = foes::posture(animal).value_or(0);
                b.posture.set_value(p);
                place(b.posture, screen->first, screen->second + 4, 0.5, 0.0);
                if (p > 0.01) b.posture.show_passive();
                else b.posture.hide();
                wcmods::share("target_posture", p);
            }
        }
        // your party's: fighting, or not yet whole again
        std::erase_if(s.ally_bars, [](auto& e) {
            auto it = allies::state().all.find(e.first);
            const bool keep = game::staying(e.first) && it != allies::state().all.end() &&
                              (it->second.fighting || it->second.health < it->second.max - 0.5);
            if (!keep) remove_bar(e.second);
            return !keep;
        });
        for (auto& [cat, a] : allies::state().all)
        {
            if (!game::staying(cat) || (!a.fighting && a.health >= a.max - 0.5)) continue;
            auto it = s.ally_bars.find(cat);
            if (it == s.ally_bars.end()) it = s.ally_bars.emplace(cat, make_ally_bar()).first;
            Bar& b = it->second;
            auto at = game::at(cat);
            auto screen = at ? game::on_screen({at->x, at->y, at->z + 30}) : std::nullopt;
            if (!screen)
            {
                b.bar.hide();
                b.name.hide();
                continue;
            }
            b.bar.set_value(a.max > 0 ? a.health / a.max : 0);
            place(b.bar, screen->first, screen->second, 0.5, 1.0);
            b.bar.show_passive();
            const std::wstring label = a.down ? a.display + L"  ·  down" : a.display;
            if (b.name.text() != label) b.name.set_text(label);
            place(b.name, screen->first, screen->second - 16, 0.5, 1.0);
            b.name.show_passive();
        }
        if (shown != s.shown) wcmods::share("bars", shown);
        if (target_shown != s.target_shown) wcmods::share("target_bar", target_shown);
        if (marked != s.marked) wcmods::share("mark", marked ? wcmods::Json(static_cast<double>(game::id(marked))) : wcmods::Json());
        if (warned != s.warned) wcmods::share("warning", warned ? wcmods::Json(static_cast<double>(game::id(warned))) : wcmods::Json());
        s.shown = shown;
        s.target_shown = target_shown;
        s.marked = marked;
        s.warned = warned;
    }

    // Real-time combat off: everything taken off the HUD.
    inline auto clear() -> void
    {
        State& s = state();
        clear_bars();
        if (s.flask) s.flask.hide();
        for (Widget& p : s.pips) p.hide();
        if (s.shown != 0) wcmods::share("bars", 0);
        s.shown = 0;
        wcmods::share("target_bar", false);
        s.target_shown = false;
        if (s.marked) wcmods::share("mark", wcmods::Json());
        if (s.warned) wcmods::share("warning", wcmods::Json());
        s.marked = nullptr;
        s.warned = nullptr;
    }
} // namespace rtc::hud
