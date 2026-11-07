// cats.hpp: fights with Clan life's cats (wcmods' docs/plans/rivals-and-motives.md, Part B). Clan life tags its cats
// ("ClanLife.cat"); they fight as cats (rules.hpp's "cat" profiles and the cats' quick claw), and a cat beaten doesn't
// vanish as an animal does: it yields (enemies.hpp), down and waiting. Then it's your choice: the finish key (H, or the
// d-pad's down) within reach gives the killing blow, the worst thing a warrior can do to a beaten foe (Clan life judges
// it: Part E); walking away (7 m), or leaving it be a while, spares it.
//
// Clan life hears it all through two shared values:
//   RealtimeCombat.fighting    the names of the cats in a fight with your cat or yielded to it (Clan life leaves them be)
//   RealtimeCombat.cat_events  the last 30 things that happened, each {n (counting up), what ("fought", "yielded",
//                              "spared", "killed"), id (Clan life's id for the cat), name}
// Shared for tests too: "cat_events" (how many), "yielded" (how many cats are down now).
#pragma once

#include <WCMods/WCMods.hpp>

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "enemies.hpp"
#include "foes.hpp"
#include "game.hpp"
#include "input.hpp"
#include "rules.hpp"

namespace rtc::cats
{
    using wcmods::UObject;

    struct Down
    {
        int id = -1;
        std::wstring name;
        double since = 0;      // seconds down
        double away = 0;       // seconds your cat's been out of reach of it
        bool killing = false;  // the blow given: its end playing
        double killed_at = 0;  // seconds since the blow
    };
    struct State
    {
        std::map<UObject*, Down> down;
        int n = 0;                         // events so far
        wcmods::Json events = wcmods::Json::array();
        double publish = 0;                // seconds to sharing who's fighting again
        std::wstring fighting_last;
        std::set<UObject*> told_fought;    // cats whose fight with your cat Clan life has heard of
        bool hinted = false;               // the killing blow's hint showing
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline constexpr double finish_reach = 300;   // cm, in front of your cat
    inline constexpr double spare_away = 700;     // cm: this far for
    inline constexpr double spare_after = 2.0;    // seconds, it's spared
    inline constexpr double left_be = 25.0;       // seconds down with nothing done: spared

    // Something happened, for Clan life.
    inline auto tell(const char* what, int id, const std::wstring& name) -> void
    {
        State& s = state();
        wcmods::Json e = wcmods::Json::object();
        e.set("n", wcmods::Json(++s.n));
        e.set("what", wcmods::Json(std::string(what)));
        e.set("id", wcmods::Json(id));
        e.set("name", wcmods::Json(wcmods::utf8(name)));
        wcmods::Json kept = wcmods::Json::array();
        const auto& old = s.events.values();
        for (size_t i = old.size() > 29 ? old.size() - 29 : 0; i < old.size(); ++i) kept.push(old[i]);
        kept.push(e);
        s.events = kept;
        wcmods::share("RealtimeCombat.cat_events", s.events);
        wcmods::share("cat_events", static_cast<double>(s.n));
        wcmods::log(L"{} {}", wcmods::wide(what), name);
    }

    // A cat yielded (enemies.hpp).
    inline auto yielded(UObject* cat, const enemies::Enemy& e) -> void
    {
        foes::release(cat, false);
        state().down[cat] = {e.id, e.display};
        tell("yielded", e.id, e.display);
        wcmods::share("yielded", static_cast<double>(state().down.size()));
    }

    // The yielded cat in reach in front of your cat, or nullptr.
    inline auto in_reach() -> UObject*
    {
        UObject* me = wcmods::player();
        auto at = game::at(me);
        auto yaw = game::facing(me);
        if (!at || !yaw) return nullptr;
        UObject* best = nullptr;
        double nearest = finish_reach;
        for (auto& [cat, d] : state().down)
        {
            if (d.killing) continue;
            auto them = game::at(cat);
            if (!them || !::rtc::in_reach(*at, *yaw, *them, finish_reach, 70)) continue;
            const double dist = flat_distance(*at, *them);
            if (dist <= nearest) nearest = dist, best = cat;
        }
        return best;
    }

    // Let it go: up again, whole, Clan life's once more.
    inline auto spare(UObject* cat, Down& d) -> void
    {
        if (game::staying(cat)) wcmods::stop_move(cat, 0.4);
        enemies::forget_yielded(cat);
        tell("spared", d.id, d.name);
    }

    // Every frame while real-time combat is on.
    inline auto frame(double dt, const input::Pressed& pressed) -> void
    {
        State& s = state();
        UObject* me = wcmods::player();
        auto at = game::at(me);
        // the killing blow: your cat's heavy strike, then the cat lies still
        if (pressed[input::Action::Finish])
            if (UObject* cat = in_reach())
            {
                Down& d = s.down[cat];
                d.killing = true;
                wcmods::face(me, cat);
                wcmods::play_montage(me, spec(Attack::Heavy).tag, spec(Attack::Heavy).rate);
            }
        // a cat your cat's blows have landed on: a fight, for Clan life (once a fight: the record goes when it's over)
        for (auto& [actor, e] : enemies::state().all)
            if (e.cat && e.hits > 0 && !s.told_fought.contains(actor))
            {
                s.told_fought.insert(actor);
                tell("fought", e.id, e.display);
            }
        std::erase_if(s.told_fought, [](UObject* a) { return !enemies::known(a); });
        std::vector<UObject*> done;
        for (auto& [cat, d] : s.down)
        {
            if (!game::staying(cat))
            {
                done.push_back(cat);
                continue;
            }
            d.since += dt;
            if (d.killing)
            {
                const double before = d.killed_at;
                d.killed_at += dt;
                if (before < 0.8 && d.killed_at >= 0.8) wcmods::play_montage(cat, L"Animation.Combat.Lose.2");
                if (before < 2.4 && d.killed_at >= 2.4)
                {
                    wcmods::set_pose(cat, wcmods::Pose::Still);
                    tell("killed", d.id, d.name);
                    enemies::forget_yielded(cat);
                    done.push_back(cat);   // (Clan life takes the body away)
                }
                continue;
            }
            auto them = game::at(cat);
            d.away = at && them && flat_distance(*at, *them) > spare_away ? d.away + dt : 0;
            if (d.away >= spare_after || d.since >= left_be)
            {
                spare(cat, d);
                done.push_back(cat);
            }
        }
        for (UObject* c : done) s.down.erase(c);
        if (!done.empty()) wcmods::share("yielded", static_cast<double>(s.down.size()));
        // what's shown: a hint by a yielded cat in reach
        const bool hint = in_reach() != nullptr;
        if (hint != s.hinted)
        {
            s.hinted = hint;
            if (hint)
                wcmods::hud_objective({.title = L"Yielded", .activity = L"H (d-pad down): the killing blow. Walk away: spare them."});
            else wcmods::hide_objective();
        }
        // who Clan life should leave be: those fighting your cat and those down
        s.publish -= dt;
        if (s.publish <= 0)
        {
            s.publish = 0.25;
            wcmods::Json names = wcmods::Json::array();
            std::wstring all;
            for (auto& [cat, e] : foes::state().all)
                if (game::clan_cat(cat)) names.push(wcmods::Json(wcmods::utf8(wcmods::name(cat)))), all += wcmods::name(cat);
            for (auto& [cat, d] : s.down) names.push(wcmods::Json(wcmods::utf8(wcmods::name(cat)))), all += wcmods::name(cat);
            if (all != s.fighting_last)
            {
                s.fighting_last = all;
                wcmods::share("RealtimeCombat.fighting", names);
            }
        }
    }

    // Real-time combat off: every cat down spared (one given the killing blow killed), nobody fighting.
    inline auto clear() -> void
    {
        State& s = state();
        for (auto& [cat, d] : s.down)
        {
            if (!game::staying(cat)) continue;
            if (!d.killing) spare(cat, d);
            else
            {
                wcmods::set_pose(cat, wcmods::Pose::Still);
                tell("killed", d.id, d.name);
            }
        }
        s.down.clear();
        s.told_fought.clear();
        s.fighting_last.clear();
        s.hinted = false;
        wcmods::share("RealtimeCombat.fighting", wcmods::Json::array());
        wcmods::hide_objective();
    }

    // The hooks, set once as the mod starts.
    inline auto hook() -> void
    {
        enemies::state().on_yield = [](UObject* cat, const enemies::Enemy& e) { yielded(cat, e); };
    }
} // namespace rtc::cats
