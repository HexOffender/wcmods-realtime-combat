// enemies.hpp: the animals in a real-time fight. The game keeps no health for a roaming animal (its stats only exist in
// a turn-based fight), so the mod keeps its own: rules.hpp's enemy_health(), by its kind and level, made as it's first
// hit. A hit takes health off, plays a hit reaction and pushes it back; at none left it plays its "lose" animation and
// is taken out of the world a few seconds later (its generator puts a new one out in time), and prey goes on your
// Clan's fresh-kill pile (rules.hpp's fresh_kill_for).
//
// Shared for tests: "hp <address>" and "hp_max <address>" (each animal hit), "defeated" (how many so far),
// "last_defeated" (the last one's address).
#pragma once

#include <WCMods/WCMods.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "game.hpp"
#include "rules.hpp"

namespace rtc::enemies
{
    using wcmods::UObject;

    struct Enemy
    {
        double health = 0, max = 0;
        int level = 1;
        std::wstring model;
        bool defeated = false;
        double gone_in = 0;   // seconds until a defeated one is taken out of the world
        int hits = 0;         // how many times your cat's hit it
        double riposte_left = 0, riposte = 1;   // parried: seconds its next hit from your cat does `riposte` times
    };
    struct State
    {
        std::map<UObject*, Enemy> all;
        int defeated = 0, ripostes = 0;
        double health_multiplier = 1;
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline auto load() -> void
    {
        std::lock_guard lock(wcmods::settings_lock());
        const double m = wcmods::settings()["enemy_health"].number(1);
        state().health_multiplier = m > 0.05 && m < 20 ? m : 1;
    }

    // Its level: its kind's (a spawned one's "...L6"), else your cat's.
    inline auto level_of(UObject* animal) -> int
    {
        for (const wcmods::Animal& a : game::animals(400))
            if (a.actor == animal && !a.kind.empty())
                if (auto l = level_of_kind(a.kind)) return *l;
        if (int32_t* l = wcmods::cat().level()) return *l;
        return 1;
    }

    inline auto share(UObject* animal, const Enemy& e) -> void
    {
        wcmods::share(std::format("hp {}", game::id(animal)), e.health);
        wcmods::share(std::format("hp_max {}", game::id(animal)), e.max);
    }

    // Its shared health taken back (an address goes to another object once this one's gone).
    inline auto unshare(UObject* animal) -> void
    {
        wcmods::share(std::format("hp {}", game::id(animal)), wcmods::Json());
        wcmods::share(std::format("hp_max {}", game::id(animal)), wcmods::Json());
    }

    // The mod's record of an animal, made the first time it's asked for.
    inline auto of(UObject* animal) -> Enemy&
    {
        auto& all = state().all;
        auto it = all.find(animal);
        if (it != all.end()) return it->second;
        Enemy e;
        e.model = wcmods::name(wcmods::get(animal, L"Mesh.SkeletalMesh").object);
        e.level = level_of(animal);
        e.max = e.health = enemy_health(e.model, e.level, state().health_multiplier);
        share(animal, e);
        return all.emplace(animal, e).first->second;
    }

    // A parry: for `seconds`, your cat's next hit on it does `times` its damage.
    inline auto open_to_riposte(UObject* animal, double seconds, double times) -> void
    {
        Enemy& e = of(animal);
        e.riposte_left = seconds;
        e.riposte = times;
    }

    // Forget an animal (it's gone back to roaming): whole again if it fights another time.
    inline auto forget(UObject* animal) -> void
    {
        auto& all = state().all;
        auto it = all.find(animal);
        if (it == all.end() || it->second.defeated) return;
        unshare(animal);
        all.erase(it);
    }

    // Whether the mod keeps a record of it (it's been hit, so it's in a fight).
    inline auto known(UObject* animal) -> bool { return state().all.contains(animal); }

    // How many times your cat's hit it (0 for one it hasn't).
    inline auto hits_of(UObject* animal) -> int
    {
        auto it = state().all.find(animal);
        return it == state().all.end() ? 0 : it->second.hits;
    }

    // The animals your cat has hit that are still standing.
    inline auto hit_animals() -> std::vector<UObject*>
    {
        std::vector<UObject*> out;
        for (auto& [animal, e] : state().all)
            if (!e.defeated && e.hits > 0) out.push_back(animal);
        return out;
    }

    inline auto defeated(UObject* animal) -> bool
    {
        auto& all = state().all;
        auto it = all.find(animal);
        return it != all.end() && it->second.defeated;
    }

    // A hit from your cat: health off, a hit reaction, pushed away from `from` at `push` cm/s. True when it was the last.
    inline auto hit(UObject* animal, double damage, Vec from, double push) -> bool
    {
        Enemy& e = of(animal);
        if (e.defeated) return false;
        ++e.hits;
        if (e.riposte_left > 0)   // a riposte: parried, open
        {
            damage *= e.riposte;
            e.riposte_left = 0;
            wcmods::share("ripostes", ++state().ripostes);
        }
        e.health = after_hit(e.health, damage);
        share(animal, e);
        auto at = game::at(animal);
        if (e.health <= 0)
        {
            e.defeated = true;
            e.gone_in = 4.0;
            wcmods::stop_ai(animal);
            wcmods::play_montage(animal, L"Animation.Combat.Lose.1");
            State& s = state();
            ++s.defeated;
            wcmods::share("defeated", s.defeated);
            wcmods::share("last_defeated", static_cast<double>(game::id(animal)));
            wcmods::log(L"{} (level {}) defeated", e.model, e.level);
            // prey feeds your Clan: onto the fresh-kill pile
            const double food = fresh_kill_for(e.model);
            if (auto pile = wcmods::fresh_kill(); pile && food > 0)
            {
                *pile.value = static_cast<float>(std::min<double>(*pile.max, *pile.value + food));
                wcmods::share("fresh_kill_given", food);
            }
            return true;
        }
        wcmods::play_montage(animal, L"Animation.Combat.Hit.1.Neutral");
        if (at && push > 0)
        {
            const double yaw = yaw_to(from, *at) / degrees_per_radian;
            game::launch(animal, {std::cos(yaw) * push, std::sin(yaw) * push, push * 0.3});
        }
        return false;
    }

    // Every frame while real-time combat is on: the defeated taken away once their animation's done, and the records
    // of animals that went (taken out, a level gone) forgotten.
    inline auto frame(double dt) -> void
    {
        auto& all = state().all;
        for (auto it = all.begin(); it != all.end();)
        {
            UObject* animal = it->first;
            Enemy& e = it->second;
            if (e.riposte_left > 0) e.riposte_left -= dt;
            if (!game::staying(animal))
            {
                unshare(animal);
                it = all.erase(it);
                continue;
            }
            if (e.defeated)
            {
                e.gone_in -= dt;
                if (e.gone_in <= 0)
                {
                    wcmods::remove_animal(animal);
                    unshare(animal);
                    it = all.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    // Real-time combat off: the records go (the defeated are taken away now).
    inline auto clear() -> void
    {
        for (auto& [animal, e] : state().all)
        {
            if (e.defeated && game::staying(animal)) wcmods::remove_animal(animal);
            unshare(animal);
        }
        state().all.clear();
    }
} // namespace rtc::enemies
