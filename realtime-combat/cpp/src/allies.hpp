// allies.hpp: your party fighting beside you (wcmods' docs/plans/rivals-and-motives.md, Part B2). Clan life shares the
// names of the clanmates with you ("ClanLife.party"); while a fight is on (an animal or a cat fighting your cat, or hit
// by it, within 25 m), this mod takes them over from Clan life (they're in "RealtimeCombat.fighting" then: cats.hpp
// shares it) and drives them:
//   - each goes after a foe: the one you're locked on to, else the one nearest it; never a cat of its own Clan (against
//     those it stands back, near you)
//   - it walks to its foe; when that gets it no nearer (no way there on the cats' walking mesh: a rabbit on a ledge), it
//     runs straight at it (swept, stopping at whatever's in the way)
//   - in reach, it strikes with the cats' quick claw (rules.hpp's cat_foe_spec), a cat's pace between strikes; the blow
//     goes through the foe's guard, hops and counters as yours does (foes::take), then lands (enemies::hit)
//   - a foe's swing that finds it in front hurts it as it would your cat; beaten, it goes down until the fight's over
//   - your cat's blows pass it by (game::allies: not among the fighters)
// When the fight's over (no foe for 1.5 s) it's given back to Clan life, which has it follow you again; one that was
// down gets up hurt, and health comes back as you go on. Clan life hears of each ("fought_beside", "fell_beside" in
// RealtimeCombat.cat_events, through cats.hpp).
//
// Shared for tests: "allies" (how many party cats are near), "ally_hits" (blows they've landed), "ally_hurt" (blows
// they've taken), "ally_runs" (frames spent running straight at a foe they couldn't walk to), "allies_down",
// "allies_fighting".
#pragma once

#include <WCMods/WCMods.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "enemies.hpp"
#include "foes.hpp"
#include "game.hpp"
#include "lockon.hpp"
#include "rules.hpp"

namespace rtc::allies
{
    using wcmods::UObject;

    struct Ally
    {
        std::wstring display;     // its cat's name, for the HUD and Clan life
        std::wstring clan;        // "ThunderClan"...
        int id = -1;              // Clan life's id for it
        int level = 1;
        double health = 0, max = 0;
        bool fighting = false;    // this mod's now (taken from Clan life)
        bool down = false;
        bool struck = false;      // landed a blow this fight
        bool fell = false;        // went down this fight
        UObject* target{};        // the foe it's after
        bool standing_back = false;
        bool running_in = false;  // no way there on its walk (a ledge, a rock): straight at its foe instead
        double best_gap = 1e9;    // the nearest it's been to its foe, walking
        double stuck = 0;         // seconds it's got no nearer
        double cooldown = 0;      // seconds to its next strike
        double strike = -1;       // seconds to its strike landing (-1: none playing)
    };
    struct State
    {
        std::map<UObject*, Ally> all;   // the party's cats near your cat
        std::set<std::wstring> names;   // Clan life's party, by actor name
        double look = 0;                // seconds to looking for them again
        double calm = 0;                // seconds with no foe near
        int hits = 0, hurt = 0, runs = 0;
        int fighting_shown = -1;        // the "allies_fighting" last shared
        std::mt19937 rng{std::random_device{}()};
        std::function<void(const char* what, int id, const std::wstring& name)> tell;   // cats.hpp's
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }
    inline auto roll() -> double
    {
        if (auto fixed = foes::state().fixed_roll) return *fixed;
        return std::uniform_real_distribution<double>(0, 1)(state().rng);
    }

    inline constexpr double fight_radius = 2500;   // cm from your cat: the foes that make a fight
    inline constexpr double damage_part = 0.8;     // an ally's blow, against your cat's light attack at its level

    // The cats driven now (in a fight, or down in one): Clan life leaves them be.
    inline auto in_fight() -> std::vector<UObject*>
    {
        std::vector<UObject*> out;
        for (auto& [a, ally] : state().all)
            if (ally.fighting && game::staying(a)) out.push_back(a);
        return out;
    }

    // Its Clan, from Clan life's tags ("ClanLife.ThunderClan").
    inline auto clan_of(UObject* cat) -> std::wstring
    {
        for (const std::wstring& t : wcmods::actor_tags(cat))
            if (t.rfind(L"ClanLife.", 0) == 0 && t.size() > 13 && t.ends_with(L"Clan")) return t.substr(9);
        return L"";
    }

    // The foes of a fight near your cat: those fighting it, and those it (or the party) has hit, not beaten.
    inline auto foes_near() -> std::vector<UObject*>
    {
        std::vector<UObject*> out;
        auto me = game::at(wcmods::player());
        if (!me) return out;
        auto add = [&](UObject* f) {
            if (!game::staying(f) || enemies::defeated(f) || state().all.contains(f)) return;
            if (std::find(out.begin(), out.end(), f) != out.end()) return;
            auto at = game::at(f);
            if (at && flat_distance(*me, *at) <= fight_radius) out.push_back(f);
        };
        for (auto& [f, _] : foes::state().all) add(f);
        for (auto& [f, e] : enemies::state().all)
            if (e.hits > 0) add(f);
        return out;
    }

    // The party's cats near, from Clan life's names, twice a second; records made as they come, let go as they leave.
    inline auto find(double dt) -> void
    {
        State& s = state();
        if ((s.look -= dt) > 0) return;
        s.look = 0.5;
        s.names.clear();
        for (const wcmods::Json& n : wcmods::shared("ClanLife.party").values()) s.names.insert(wcmods::wide(n.string()));
        std::set<UObject*> seen;
        if (!s.names.empty())
            for (const wcmods::Nearby& c : wcmods::nearby(wcmods::Kind::Cats, 6000))
            {
                if (!s.names.contains(c.name) || !game::clan_cat(c.actor)) continue;
                seen.insert(c.actor);
                if (s.all.contains(c.actor)) continue;
                Ally a;
                a.display = game::cat_tag(c.actor, L"ClanLife.name.");
                a.clan = clan_of(c.actor);
                a.level = cat_level(game::cat_tag(c.actor, L"ClanLife.rank."));
                try
                {
                    a.id = std::stoi(game::cat_tag(c.actor, L"ClanLife.id."));
                }
                catch (...)
                {
                    a.id = -1;
                }
                a.max = a.health = enemy_health(L"cat", a.level);
                s.all[c.actor] = a;
            }
        std::erase_if(s.all, [&](auto& e) {
            if (seen.contains(e.first)) return false;
            if (e.second.fighting && game::staying(e.first)) wcmods::stop_following(e.first, false);
            return true;
        });
        game::allies() = seen;
        wcmods::share("allies", static_cast<double>(s.all.size()));
    }

    // The fight's over for it: given back to Clan life (which has it follow you again), up again if it was down, and
    // Clan life told what it did.
    inline auto release(UObject* a, Ally& ally) -> void
    {
        if (game::staying(a))
        {
            wcmods::stop_following(a, false);
            wcmods::stop_moving(a);
            if (ally.down) wcmods::stop_move(a, 0.4);
        }
        if (ally.down) ally.health = std::max(ally.health, ally.max * 0.4);
        State& s = state();
        if (s.tell && ally.struck) s.tell("fought_beside", ally.id, ally.display);
        if (s.tell && ally.fell) s.tell("fell_beside", ally.id, ally.display);
        ally.fighting = ally.down = ally.struck = ally.fell = ally.standing_back = ally.running_in = false;
        ally.best_gap = 1e9, ally.stuck = 0;
        ally.target = nullptr;
        ally.strike = -1;
    }

    // A foe's attack landing (foes.hpp): each of the party in front of it, in reach, is hurt as your cat would be.
    inline auto swing(UObject* foe) -> void
    {
        State& s = state();
        const FoeSpec& spec = foes::spec_for(foe);
        auto at = game::at(foe);
        auto facing = game::facing(foe);
        if (!at || !facing) return;
        const enemies::Enemy& e = enemies::of(foe);
        const double damage = enemy_damage(e.model, e.level, foes::state().damage_multiplier);
        for (auto& [a, ally] : s.all)
        {
            if (!ally.fighting || ally.down || !game::staying(a)) continue;
            auto them = game::at(a);
            if (!them || !in_reach(*at, *facing, *them, spec.reach, spec.cone)) continue;
            ally.health = std::max(0.0, ally.health - damage);
            wcmods::share("ally_hurt", ++s.hurt);
            ally.strike = -1;
            if (ally.health > 0)
            {
                wcmods::play_montage(a, L"Animation.Combat.Hit.1.Neutral");
                continue;
            }
            // beaten: down until the fight's over
            ally.down = ally.fell = true;
            ally.target = nullptr;
            wcmods::stop_following(a, false);
            wcmods::stop_moving(a);
            wcmods::play_montage(a, L"Animation.Combat.Lose.1");
        }
    }

    // Its foe: the one you're locked on to, else the one nearest it; none of its own Clan.
    inline auto pick(UObject* a, const Ally& ally, const std::vector<UObject*>& foes) -> UObject*
    {
        auto allowed = [&](UObject* f) { return ally.clan.empty() || !wcmods::has_tag(f, L"ClanLife." + ally.clan); };
        UObject* locked = lockon::locked();
        if (locked && std::find(foes.begin(), foes.end(), locked) != foes.end() && allowed(locked)) return locked;
        auto me = game::at(a);
        UObject* best = nullptr;
        double nearest = 1e18;
        for (UObject* f : foes)
        {
            if (!allowed(f)) continue;
            auto at = game::at(f);
            if (!me || !at) continue;
            const double d = flat_distance(*me, *at);
            if (d < nearest) nearest = d, best = f;
        }
        return best;
    }

    // Every frame while real-time combat is on.
    inline auto frame(double dt) -> void
    {
        State& s = state();
        find(dt);
        if (s.all.empty())
        {
            if (s.fighting_shown != 0) wcmods::share("allies_fighting", 0.0), wcmods::share("allies_down", 0.0);
            s.fighting_shown = 0;
            return;
        }
        UObject* me = wcmods::player();
        const auto foes = foes_near();
        s.calm = foes.empty() ? s.calm + dt : 0;
        const FoeSpec& spec = cat_foe_spec();
        int down = 0, fighting = 0;
        for (auto& [a, ally] : s.all)
        {
            if (!game::staying(a)) continue;
            // the fight over: back to Clan life; health back as you go on
            if (s.calm >= 1.5)
            {
                if (ally.fighting) release(a, ally);
                ally.health = std::min(ally.max, ally.health + ally.max * 0.03 * dt);
                continue;
            }
            if (foes.empty()) continue;
            if (!ally.fighting)
            {
                ally.fighting = true;
                ally.cooldown = 0.4 + 0.8 * roll();
                wcmods::stop_following(a, false);   // (Clan life's follow of your cat)
            }
            ++fighting;
            if (ally.down)
            {
                ++down;
                continue;
            }
            // its foe, and after it
            UObject* target = pick(a, ally, foes);
            if (target != ally.target || (target && !ally.running_in && !wcmods::following(a)))
            {
                if (target != ally.target) ally.running_in = false, ally.best_gap = 1e9, ally.stuck = 0;
                ally.target = target;
                ally.strike = -1;
                if (target) wcmods::follow(a, target, {.distance = spec.reach - 50});
            }
            if (!target)
            {
                // none it would fight: back near you
                if (!ally.standing_back || !wcmods::following(a)) wcmods::follow(a, me, {.distance = 300});
                ally.standing_back = true;
                continue;
            }
            ally.standing_back = false;
            auto at = game::at(a);
            auto them = game::at(target);
            if (!at || !them) continue;
            // closing in: its walk; when that gets it no nearer for a while (no way there on its mesh), straight at it
            const double gap = flat_distance(*at, *them);
            if (ally.strike < 0 && gap > spec.reach + 30)
            {
                if (gap < ally.best_gap - 40) ally.best_gap = gap, ally.stuck = 0;
                else ally.stuck += dt;
                if (!ally.running_in && ally.stuck > 1.2 && gap < 1500)
                {
                    ally.running_in = true;
                    wcmods::stop_following(a, false);
                    wcmods::stop_moving(a);
                }
                else if (ally.running_in && gap >= 1500)
                {
                    ally.running_in = false;   // too far for that: its walk again
                    ally.best_gap = 1e9, ally.stuck = 0;
                    wcmods::follow(a, target, {.distance = spec.reach - 50});
                }
                if (ally.running_in)
                {
                    wcmods::face(a, target);
                    game::slide(a, yaw_to(*at, *them), std::min(gap - (spec.reach - 40), 420 * dt));
                    wcmods::share("ally_runs", ++s.runs);
                }
                ally.cooldown -= dt;
                continue;
            }
            // a strike landing
            if (ally.strike >= 0)
            {
                ally.strike -= dt;
                if (ally.strike >= 0) continue;
                ally.strike = -1;
                ally.cooldown = 1.0 + 1.4 * roll();
                auto facing = game::facing(a);
                if (!facing || !in_reach(*at, *facing, *them, spec.reach + 40, spec.cone + 20) || enemies::defeated(target)) continue;
                const double part = foes::take(target, Attack::Light, *at);
                if (part <= 0) continue;
                const double damage = cat_damage(Attack::Light, ally.level) * damage_part * part;
                enemies::hit(target, damage, *at, part < 1 ? 0 : 60, part >= 1);
                ally.struck = true;
                wcmods::share("ally_hits", ++s.hits);
                continue;
            }
            // in reach: a strike
            if ((ally.cooldown -= dt) > 0) continue;
            wcmods::face(a, target);
            game::play_from(a, spec.tag, spec.from, spec.rate);
            ally.strike = (spec.hit - spec.from) / spec.rate;
        }
        wcmods::share("allies_down", static_cast<double>(down));
        wcmods::share("allies_fighting", static_cast<double>(fighting));
        s.fighting_shown = fighting;
    }

    // Real-time combat off: everyone given back to Clan life.
    inline auto clear() -> void
    {
        State& s = state();
        for (auto& [a, ally] : s.all)
            if (ally.fighting) release(a, ally);
        s.all.clear();
        game::allies().clear();
        s.calm = 0;
    }

    // The hooks, set once as the mod starts.
    inline auto hook() -> void
    {
        foes::state().on_swing = [](UObject* foe) { swing(foe); };
    }
} // namespace rtc::allies
