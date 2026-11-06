// foes.hpp: step 5, the animals fight back. An animal joins the fight when your cat comes within 4 m of it or hits it:
// its own logic stops and it follows your cat (wcmods::follow), and when it's close, its cooldown's done and no other
// animal is attacking, it faces your cat and attacks with its own default fight animation (rules.hpp's FoeSpec and
// step()). When the attack lands, your cat takes its damage (rules.hpp's enemy_damage) and plays a hit reaction, unless
// it's in a dodge's safe moment or out of reach. Hit in its wind-up, an animal reels and its attack stops. Led more
// than 20 m from where it joined (or farther than 25 m from your cat) for 3 s, it gives up and goes back to roaming
// (its logic started again). Defeated (enemies.hpp), it's let go.
//
// Your cat's health doesn't go below 1 here: falling is step 7's.
//
// Shared for tests: "foes" (how many are fighting), "foe <address>" (each one's state: "chase", "windup", "recover",
// "reel"), "foe_attacks", "cat_hits", "dodged" (attacks that found your cat in a dodge's safe moment), "missed" (that
// found it out of reach), "gave_up".
#pragma once

#include <WCMods/WCMods.hpp>

#include <format>
#include <map>
#include <random>

#include "attacks.hpp"
#include "dodge.hpp"
#include "enemies.hpp"
#include "flask.hpp"
#include "game.hpp"
#include "rules.hpp"

namespace rtc::foes
{
    using wcmods::UObject;

    struct Entry
    {
        Foe foe;
        Vec home;            // where it joined the fight
        UObject* montage{};
        int hits_seen = 0;   // enemies.hpp's hit count for it, as last seen (a new hit may stagger it)
    };
    struct State
    {
        std::map<UObject*, Entry> all;
        std::mt19937 rng{std::random_device{}()};
        double look = 0;              // seconds to the next look for animals near
        double hurt_left = 0;         // your cat's hit reaction, seconds before it's cut short
        UObject* hurt_montage{};
        int attacks = 0, cat_hits = 0, dodged = 0, missed = 0, gave_up = 0;
        double damage_multiplier = 1;
        bool fight_back = true;   // settings.json's "animals_fight_back"
        double engage = 400;      // settings.json's "engage_range_m", in cm
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline auto load() -> void
    {
        std::lock_guard lock(wcmods::settings_lock());
        const double m = wcmods::settings()["enemy_damage"].number(1);
        state().damage_multiplier = m >= 0 && m < 20 ? m : 1;
        state().fight_back = wcmods::settings()["animals_fight_back"].boolean(true);
        const double r = wcmods::settings()["engage_range_m"].number(4);
        state().engage = (r >= 0 && r <= 30 ? r : 4) * 100;
    }

    inline auto share_state(UObject* animal, const Foe& f) -> void
    {
        wcmods::share(std::format("foe {}", game::id(animal)), code(f.state));
    }
    inline auto share_count() -> void { wcmods::share("foes", static_cast<double>(state().all.size())); }

    inline auto engaged(UObject* animal) -> bool { return state().all.contains(animal); }

    inline auto engage(UObject* animal) -> void
    {
        State& s = state();
        if (s.all.contains(animal) || enemies::defeated(animal)) return;
        const FoeSpec spec;
        wcmods::follow(animal, wcmods::player(), {.distance = spec.follow, .speed = spec.speed});
        Entry e;
        e.hits_seen = enemies::hits_of(animal);
        if (auto where = game::at(animal)) e.home = *where;
        s.all[animal] = e;
        share_state(animal, e.foe);
        share_count();
        wcmods::log(L"{} joins the fight", wcmods::name(wcmods::get(animal, L"Mesh.SkeletalMesh").object));
    }

    // Let one go: its attack stopped, following ended; `roam` gives it back to its own logic.
    inline auto release(UObject* animal, bool roam) -> void
    {
        State& s = state();
        auto it = s.all.find(animal);
        if (it == s.all.end()) return;
        if (game::staying(animal))
        {
            if (it->second.montage && game::montage_on(animal, it->second.montage)) game::stop_montage(animal, it->second.montage, 0.2);
            wcmods::stop_following(animal, roam);   // roam: its tree run again
            if (roam) enemies::forget(animal);       // and whole again, if it fights another time
        }
        wcmods::share(std::format("foe {}", game::id(animal)), wcmods::Json());
        s.all.erase(it);
        share_count();
    }

    // Your cat takes a hit: health off (not below 1: falling is step 7's), its attack cut short, a short hit reaction.
    inline auto hurt_cat(double damage) -> void
    {
        State& s = state();
        UObject* me = wcmods::player();
        auto health = wcmods::cat().health();
        if (!me || !health) return;
        *health.value = static_cast<float>(std::max(1.0, *health.value - damage));
        attacks::end();
        dodge::end();   // (past its safe moment: the hit cuts the hop short)
        flask::end();   // a drink stops, the charge spent
        s.hurt_montage = wcmods::play_montage(me, L"Animation.Combat.Hit.1.Neutral", 1.2);
        s.hurt_left = 0.45;
        wcmods::share("cat_hits", ++s.cat_hits);
    }

    // The attack lands: your cat takes it if it's in reach and in front of the animal, and not in a dodge's safe moment.
    inline auto land(UObject* animal) -> void
    {
        State& s = state();
        const FoeSpec spec;
        auto them = game::at(animal);
        auto me = game::at(wcmods::player());
        auto facing = game::facing(animal);
        if (!them || !me || !facing) return;
        if (!in_reach(*them, *facing, *me, spec.reach, spec.cone))
        {
            wcmods::share("missed", ++s.missed);
            return;
        }
        if (dodge::safe())
        {
            wcmods::share("dodged", ++s.dodged);
            return;
        }
        const enemies::Enemy& e = enemies::of(animal);
        hurt_cat(enemy_damage(e.model, e.level, s.damage_multiplier));
    }

    // Every frame while real-time combat is on.
    inline auto frame(double dt) -> void
    {
        State& s = state();
        const FoeSpec spec;
        UObject* me = wcmods::player();
        auto at_me = game::at(me);
        if (!me || !at_me) return;

        // your cat's hit reaction, cut short
        if (s.hurt_left > 0)
        {
            s.hurt_left -= dt;
            if (s.hurt_left <= 0 && s.hurt_montage && game::montage_on(me, s.hurt_montage)) game::stop_montage(me, s.hurt_montage, 0.2);
        }

        // four times a second: animals near, and those your cat's hit, join
        s.look -= dt;
        if (s.look <= 0)
        {
            s.look = 0.25;
            if (s.engage > 0)
                for (const wcmods::Animal& a : game::animals(s.engage))
                    if (!enemies::defeated(a.actor)) engage(a.actor);
            for (UObject* hit : enemies::hit_animals())
                if (!enemies::defeated(hit)) engage(hit);
        }

        std::uniform_real_distribution<double> cooldown(spec.cooldown_min, spec.cooldown_max);
        bool someone_attacking = false;
        for (auto& [animal, e] : s.all) someone_attacking = someone_attacking || attacking(e.foe);

        std::vector<std::pair<UObject*, bool>> letting_go;   // (animal, back to roaming)
        for (auto& [animal, e] : s.all)
        {
            if (!game::staying(animal) || enemies::defeated(animal))
            {
                letting_go.push_back({animal, false});
                continue;
            }
            auto them = game::at(animal);
            if (!them) continue;
            // a new hit from your cat: in its wind-up, it reels
            const int hits = enemies::hits_of(animal);
            if (hits != e.hits_seen)
            {
                e.hits_seen = hits;
                if (stagger(e.foe))
                {
                    if (e.montage && game::montage_on(animal, e.montage)) game::stop_montage(animal, e.montage, 0.1);
                    share_state(animal, e.foe);
                }
            }
            const FoeState before = e.foe.state;
            const bool my_turn = !someone_attacking || attacking(e.foe);
            switch (step(e.foe, spec, dt, distance(*at_me, *them), flat_distance(e.home, *them), my_turn, cooldown(s.rng)))
            {
            case FoeDo::StartAttack:
                wcmods::stop_following(animal, false);
                wcmods::face(animal, me);
                e.montage = game::play_from(animal, spec.tag, spec.from, spec.rate);
                someone_attacking = true;
                wcmods::share("foe_attacks", ++s.attacks);
                break;
            case FoeDo::Land: land(animal); break;
            case FoeDo::EndAttack:
                if (e.montage && game::montage_on(animal, e.montage)) game::stop_montage(animal, e.montage, 0.25);
                e.montage = nullptr;
                wcmods::follow(animal, me, {.distance = spec.follow, .speed = spec.speed});
                break;
            case FoeDo::GiveUp:
                letting_go.push_back({animal, true});
                wcmods::share("gave_up", ++s.gave_up);
                break;
            case FoeDo::Nothing: break;
            }
            // early in its wind-up it keeps turning to your cat; the last 0.3 s it's committed (that's when to dodge)
            if (e.foe.state == FoeState::WindUp && e.foe.t < hit_at(spec) - 0.3) wcmods::face(animal, me);
            if (e.foe.state != before) share_state(animal, e.foe);
        }
        for (auto& [animal, roam] : letting_go) release(animal, roam);
    }

    // Real-time combat off: every animal fighting goes back to roaming.
    inline auto clear() -> void
    {
        std::vector<UObject*> all;
        for (auto& [animal, e] : state().all) all.push_back(animal);
        for (UObject* a : all) release(a, true);
        state().hurt_left = 0;
    }
} // namespace rtc::foes
