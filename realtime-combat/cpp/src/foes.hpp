// foes.hpp: the animals fight back, each the way its kind does (rules.hpp's FoeProfile and step()). An animal joins the
// fight when your cat comes within 4 m of it or hits it: its own logic stops and it comes toward your cat, but only to
// its spacing (about 2.5 m), where it paces round your cat, watching. When no other animal is attacking and its
// cooldown's done, it rushes in and attacks with its own default fight animation; some hold the wind-up a while first
// (a feint, to bait an early dodge), and some strike again straight after (a combo). After striking it's open for a
// moment, then backs off to its spacing again.
//
// When your cat starts an attack at an animal in front of it that's pacing, closing in or backing off, the animal may
// guard (it crouches, facing your cat: a hit from in front does a fifth of its damage and fills its posture, a heavy
// one twice as much; full, the guard breaks and it reels, open to a riposte; it may strike back at once instead) or hop
// aside out of reach. Mid-attack or open after one, it can't: those are the moments to hit it, and a hit in its wind-up
// stops the attack. Prey that's losing runs for it, and gets away if it's left 18 m behind.
//
// When an attack lands, your cat takes its damage (rules.hpp's enemy_damage) unless it's in a dodge's safe moment or out
// of reach; a guard (guard.hpp) takes it differently, and a parry makes the animal reel. Led more than 20 m from where it
// joined (or farther than 25 m from your cat) for 3 s, it gives up and goes back to roaming. Defeated (enemies.hpp), it's
// let go.
//
// Shared for tests: "foes" (how many are fighting), "foe <address>" (each one's state: rules.hpp's code(FoeState)),
// "foe_attacks", "dodged" (attacks that found your cat in a dodge's safe moment), "missed" (that found it out of reach),
// "gave_up", "guards", "hops", "blocked_hits", "broken_guards", "counters", "feints", "combos", "fled"; hurt.hpp's
// "cat_hits". For tests: RealtimeCombat.foe_rolls (a number from 0 to 1, or unset) makes every chance roll that number,
// and RealtimeCombat.no_flee keeps prey fighting whatever its health.
#pragma once

#include <WCMods/WCMods.hpp>

#include <format>
#include <functional>
#include <map>
#include <optional>
#include <random>

#include "attacks.hpp"
#include "dodge.hpp"
#include "enemies.hpp"
#include "flask.hpp"
#include "guard.hpp"
#include "hurt.hpp"
#include "game.hpp"
#include "rules.hpp"

namespace rtc::foes
{
    using wcmods::UClass;
    using wcmods::UObject;

    struct Entry
    {
        Foe foe;
        FoeProfile profile;
        Vec home;            // where it joined the fight
        UClass* cls{};       // its class as it joined: the very animal, not another the engine put in its place
        UObject* montage{};  // its attack or guard animation playing
        double repace = 0;   // seconds to its next pacing step
        double reflee = 0;   // seconds to asking its run again
    };
    struct State
    {
        std::map<UObject*, Entry> all;
        std::mt19937 rng{std::random_device{}()};
        double look = 0;              // seconds to the next look for animals near
        int attacks = 0, dodged = 0, missed = 0, gave_up = 0;
        int guards = 0, hops = 0, blocked = 0, broken = 0, counters = 0, feints = 0, combos = 0, fled = 0;
        double damage_multiplier = 1;
        bool fight_back = true;   // settings.json's "animals_fight_back"
        double engage = 400;      // settings.json's "engage_range_m", in cm
        // the tests' switches, read once a frame: every chance's roll fixed, and prey fighting on at any health
        std::optional<double> fixed_roll;
        bool no_flee = false;
        std::function<void(UObject*)> on_swing;   // allies.hpp's: an attack landing finds your party's cats too
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    // A chance's roll: random, or the tests' fixed number.
    inline auto roll() -> double
    {
        if (state().fixed_roll) return *state().fixed_roll;
        return std::uniform_real_distribution<double>(0, 1)(state().rng);
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
    // What its attack is: a cat's quick claw, or an animal's default fight animation.
    inline auto spec_for(UObject* actor) -> const FoeSpec&
    {
        static const FoeSpec animal;
        return game::clan_cat(actor) ? cat_foe_spec() : animal;
    }


    // Its posture as a part of what breaks its guard (0 to 1), for the HUD; nullopt for an animal not fighting.
    inline auto posture(UObject* animal) -> std::optional<double>
    {
        auto it = state().all.find(animal);
        if (it == state().all.end()) return std::nullopt;
        return std::clamp(it->second.foe.posture / it->second.profile.posture, 0.0, 1.0);
    }

    inline auto stop_animation(UObject* animal, Entry& e, double blend) -> void
    {
        if (e.montage && game::montage_on(animal, e.montage)) game::stop_montage(animal, e.montage, blend);
        e.montage = nullptr;
    }

    inline auto engage(UObject* animal) -> void
    {
        State& s = state();
        if (s.all.contains(animal) || enemies::defeated(animal)) return;
        Entry e;
        e.cls = animal->GetClassPrivate();
        e.profile = profile(enemies::of(animal).model);
        e.foe.cooldown = 0.8 + 1.2 * roll();   // a moment's look at your cat before the first attack
        if (auto where = game::at(animal)) e.home = *where;
        wcmods::follow(animal, wcmods::player(), {.distance = e.profile.spacing, .speed = e.profile.speed});
        s.all[animal] = e;
        share_state(animal, e.foe);
        share_count();
        wcmods::log(L"{} joins the fight", wcmods::name(wcmods::get(animal, L"Mesh.SkeletalMesh").object));
    }

    // Let one go: its animation stopped, its moves ended; `roam` gives it back to its own logic.
    inline auto release(UObject* animal, bool roam) -> void
    {
        State& s = state();
        auto it = s.all.find(animal);
        if (it == s.all.end()) return;
        if (game::staying(animal, it->second.cls))
        {
            stop_animation(animal, it->second, 0.2);
            wcmods::stop_following(animal, false);
            wcmods::stop_moving(animal);
            if (roam && game::clan_cat(animal)) enemies::forget(animal);   // (Clan life walks its cats: no game tree to start)
            else if (roam)
            {
                wcmods::restart_ai(animal);   // its tree run again
                enemies::forget(animal);      // and whole again, if it fights another time
            }
        }
        wcmods::share(std::format("foe {}", game::id(animal)), wcmods::Json());
        s.all.erase(it);
        share_count();
    }

    // The attack lands: your cat takes it if it's in reach and in front of the animal, and not in a dodge's safe moment,
    // through its guard (guard.hpp: blocked, broken, or parried). Whether it was parried (the caller makes it reel).
    inline auto land(UObject* animal) -> bool
    {
        State& s = state();
        const FoeSpec& spec = spec_for(animal);
        auto them = game::at(animal);
        auto me = game::at(wcmods::player());
        auto facing = game::facing(animal);
        if (!them || !me || !facing) return false;
        if (s.on_swing) s.on_swing(animal);
        if (!in_reach(*them, *facing, *me, spec.reach, spec.cone))
        {
            wcmods::share("missed", ++s.missed);
            return false;
        }
        if (dodge::safe())
        {
            wcmods::share("dodged", ++s.dodged);
            return false;
        }
        const enemies::Enemy& e = enemies::of(animal);
        return blocking::take(animal, enemy_damage(e.model, e.level, s.damage_multiplier)) == Guarded::Parried;
    }

    // Its guard: crouched, facing your cat (a stand-in: its attack's wind-up held still, low and watching).
    inline auto raise_guard(UObject* animal, Entry& e) -> void
    {
        const FoeSpec& spec = spec_for(animal);
        wcmods::stop_following(animal, false);
        wcmods::stop_moving(animal);
        wcmods::face(animal, wcmods::player());
        e.montage = game::play_from(animal, spec.tag, spec.from + 0.35 * spec.rate, spec.rate);
        if (e.montage) game::set_montage_rate(animal, e.montage, 0);
        wcmods::share("guards", ++state().guards);
    }

    // A hop aside, out of your cat's reach: the game's dodge to that side, and slid there (the dodge doesn't move it).
    inline auto hop(UObject* animal, Entry& e) -> void
    {
        stop_animation(animal, e, 0.1);
        wcmods::stop_following(animal, false);
        wcmods::stop_moving(animal);
        e.foe.side = roll() < 0.5 ? 1 : -1;
        wcmods::play_montage(animal, e.foe.side > 0 ? L"Animation.Combat.Dodge.Left" : L"Animation.Combat.Dodge.Right");
        wcmods::share("hops", ++state().hops);
    }

    // Your cat starts an attack (attacks.hpp): each animal fighting that's in front of it and near enough may guard or
    // hop aside, as its kind does.
    inline auto attack_started(Attack a) -> void
    {
        const AttackSpec& sp = spec(a);
        UObject* me = wcmods::player();
        auto at = game::at(me);
        auto yaw = game::facing(me);
        if (!at || !yaw) return;
        for (auto& [animal, e] : state().all)
        {
            auto them = game::at(animal);
            if (!them || !in_reach(*at, *yaw, *them, sp.reach + sp.lunge * 0.4 + 120, sp.cone + 15)) continue;
            switch (react(e.foe, e.profile, roll()))
            {
            case FoeReaction::Guard: raise_guard(animal, e); break;
            case FoeReaction::KeepGuard: break;
            case FoeReaction::Hop: hop(animal, e); break;
            case FoeReaction::None: continue;
            }
            share_state(animal, e.foe);
        }
    }

    // A quick wind-up (a combo's second strike, or a counter): the attack played again from just before its strike.
    inline auto quick_strike(UObject* animal, Entry& e) -> void
    {
        const FoeSpec& spec = spec_for(animal);
        wcmods::face(animal, wcmods::player());
        e.montage = game::play_from(animal, spec.tag, std::max(spec.from, spec.hit - spec.quick * spec.rate), spec.rate);
    }

    // Your cat's hit landing on it (attacks.hpp): the part of the hit's damage it takes (0: none), its guard, hop and
    // wind-up seen to. An animal not fighting yet takes the whole hit.
    inline auto take(UObject* animal, Attack a, Vec from) -> double
    {
        State& s = state();
        auto it = s.all.find(animal);
        if (it == s.all.end()) return 1;
        Entry& e = it->second;
        auto them = game::at(animal);
        auto facing = game::facing(animal);
        const bool front = them && facing && std::abs(turn(*facing, yaw_to(*them, from))) <= 75;
        const FoeSpec& spec = spec_for(animal);
        const FoeHitResult r = take_hit(e.foe, spec, e.profile, front, a == Attack::Heavy, roll());
        switch (r.what)
        {
        case FoeHit::Missed: break;
        case FoeHit::Blocked: wcmods::share("blocked_hits", ++s.blocked); break;
        case FoeHit::Countered:
            wcmods::share("blocked_hits", ++s.blocked);
            wcmods::share("counters", ++s.counters);
            quick_strike(animal, e);
            break;
        case FoeHit::Broken:
            stop_animation(animal, e, 0.1);
            wcmods::play_montage(animal, L"Animation.Combat.Hit.1.Neutral");
            enemies::open_to_riposte(animal, spec.broken_for, GuardSpec{}.riposte);
            wcmods::share("broken_guards", ++s.broken);
            break;
        case FoeHit::Staggered: stop_animation(animal, e, 0.1); break;
        case FoeHit::Hit: break;
        }
        share_state(animal, e.foe);
        return r.part;
    }

    // Pacing: a few steps round your cat on its side, at its spacing.
    inline auto pace_step(UObject* animal, Entry& e, Vec me) -> void
    {
        auto them = game::at(animal);
        if (!them) return;
        const double bearing = yaw_to(me, *them) + e.foe.side * 35;
        const double r = bearing / degrees_per_radian;
        const wcmods::Vec3 to{me.x + std::cos(r) * e.profile.spacing, me.y + std::sin(r) * e.profile.spacing, them->z};
        wcmods::move_to(animal, to, {.speed = e.profile.pace_speed, .radius = 40});
    }

    // Every frame while real-time combat is on.
    inline auto frame(double dt) -> void
    {
        State& s = state();
        UObject* me = wcmods::player();
        auto at_me = game::at(me);
        if (!me || !at_me) return;
        const wcmods::Json fixed = wcmods::shared("RealtimeCombat.foe_rolls");
        s.fixed_roll = fixed.is_number() ? std::optional<double>(std::clamp(fixed.number(), 0.0, 0.999)) : std::nullopt;
        s.no_flee = wcmods::shared("RealtimeCombat.no_flee").boolean(false);

        // four times a second: animals near, and those your cat's hit, join
        s.look -= dt;
        if (s.look <= 0)
        {
            s.look = 0.25;
            if (s.engage > 0)
            {
                for (const wcmods::Animal& a : game::animals(s.engage))
                    if (!enemies::defeated(a.actor)) engage(a.actor);
                // a Clan life cat only by itself when Clan life marks it hostile (and your cat hasn't just spared it);
                // the rest when your cat strikes them
                for (const wcmods::Animal& c : game::cats(s.engage))
                    if (game::hostile(c.actor) && !enemies::defeated(c.actor) && !enemies::recently_spared(c.actor)) engage(c.actor);
            }
            for (UObject* hit : enemies::hit_animals())
                if (!enemies::defeated(hit)) engage(hit);
        }

        bool someone_attacking = false;
        for (auto& [animal, e] : s.all) someone_attacking = someone_attacking || attacking(e.foe);

        std::vector<std::pair<UObject*, bool>> letting_go;   // (animal, back to roaming)
        // Over a copy of the fighters: a blow that lands here can fell your cat, and the fall ends every fight (clear)
        // under this loop, so each is looked up again before it's touched (seen in game: a crash in a camp brawl).
        std::vector<UObject*> fighting;
        for (auto& [animal, e] : s.all) fighting.push_back(animal);
        for (UObject* animal : fighting)
        {
            auto found = s.all.find(animal);
            if (found == s.all.end()) continue;   // let go while this loop ran
            Entry& e = found->second;
            if (!game::staying(animal, e.cls) || enemies::defeated(animal))
            {
                letting_go.push_back({animal, false});
                continue;
            }
            auto them = game::at(animal);
            if (!them) continue;
            const FoeSpec& spec = spec_for(animal);
            const enemies::Enemy& rec = enemies::of(animal);
            // (RealtimeCombat.no_flee, for tests: prey fights on whatever its health)
            const double health = rec.max > 0 && !s.no_flee ? rec.health / rec.max : 1;
            const double dist = distance(*at_me, *them);
            const FoeState before = e.foe.state;
            const bool my_turn = !someone_attacking || attacking(e.foe);
            switch (step(e.foe, spec, e.profile, dt, dist, flat_distance(e.home, *them), my_turn, health, roll))
            {
            case FoeDo::Approach:
                stop_animation(animal, e, 0.2);
                wcmods::follow(animal, me, {.distance = e.profile.spacing, .speed = e.profile.speed});
                break;
            case FoeDo::Pace:
                stop_animation(animal, e, 0.2);
                wcmods::stop_following(animal, false);
                e.repace = 0;
                break;
            case FoeDo::StartAttack:
                wcmods::stop_following(animal, false);
                wcmods::stop_moving(animal);
                wcmods::face(animal, me);
                e.montage = game::play_from(animal, spec.tag, spec.from, spec.rate);
                someone_attacking = true;
                wcmods::share("foe_attacks", ++s.attacks);
                if (e.foe.hold > 0) wcmods::share("feints", ++s.feints);
                break;
            case FoeDo::Hold:
                if (e.montage) game::set_montage_rate(animal, e.montage, 0.06);
                break;
            case FoeDo::Release:
                if (e.montage) game::set_montage_rate(animal, e.montage, spec.rate);
                break;
            case FoeDo::Land:
                // parried: it reels, open to a riposte (unless its blow felled your cat, which ended the fight: `e` is gone)
                if (land(animal) && s.all.contains(animal))
                {
                    parried(e.foe, GuardSpec{}.parry_reel);
                    stop_animation(animal, e, 0.1);
                    wcmods::play_montage(animal, L"Animation.Combat.Hit.1.Neutral");
                }
                break;
            case FoeDo::Combo:
                quick_strike(animal, e);
                wcmods::share("combos", ++s.combos);
                break;
            case FoeDo::Retreat:
                stop_animation(animal, e, 0.25);
                break;
            case FoeDo::Flee:
                stop_animation(animal, e, 0.2);
                wcmods::stop_following(animal, false);
                e.reflee = 0;
                wcmods::share("fled", ++s.fled);
                break;
            case FoeDo::GiveUp:
                letting_go.push_back({animal, true});
                wcmods::share("gave_up", ++s.gave_up);
                break;
            case FoeDo::Nothing: break;
            }
            if (!s.all.contains(animal)) continue;   // the fight ended in that step (your cat fell)

            // what each state does over time
            switch (e.foe.state)
            {
            case FoeState::Pace:
                e.repace -= dt;
                if (e.repace <= 0)
                {
                    e.repace = 0.6;
                    pace_step(animal, e, *at_me);
                }
                break;
            case FoeState::WindUp:
            {
                // early on it turns to your cat and rushes in to striking range; the last moment it's committed (dodge
                // then); held in a feint, it keeps watching
                const double committed = e.foe.quick ? e.foe.strike_at - spec.commit : commit_at(spec);
                if (e.foe.t < committed || (e.foe.held && e.foe.t < e.foe.hold_at + e.foe.hold)) wcmods::face(animal, me);
                if (dist > e.profile.attack_range && !e.foe.landed)
                {
                    const double left = std::max(0.15, e.foe.strike_at - e.foe.t - 0.05);
                    const double gap = dist - e.profile.attack_range;
                    game::slide(animal, yaw_to(*them, *at_me), std::min({gap / left * dt, 700 * dt, gap}));
                }
                break;
            }
            case FoeState::Retreat:
                // backs off, still facing your cat, to its spacing
                if (dist < e.profile.spacing) game::slide(animal, yaw_to(*at_me, *them), std::min(e.profile.spacing - dist, 320 * dt));
                wcmods::face(animal, me);
                break;
            case FoeState::Hop:
                if (e.foe.t < spec.hop) game::slide(animal, yaw_to(*at_me, *them) + e.foe.side * 90, 330 * dt);
                break;
            case FoeState::Guard: wcmods::face(animal, me); break;
            case FoeState::Flee:
                e.reflee -= dt;
                if (e.reflee <= 0)
                {
                    e.reflee = 1.0;
                    wcmods::flee(animal, me, 2500, wcmods::MoveOptions{.speed = e.profile.speed * 1.2, .radius = 80});
                }
                break;
            default: break;
            }
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
    }

    // The hooks attacks.hpp calls, set once as the mod starts.
    inline auto hook() -> void
    {
        attacks::state().on_start = [](Attack a) { attack_started(a); };
        attacks::state().on_land = [](UObject* animal, Attack a, Vec from) { return take(animal, a, from); };
    }
} // namespace rtc::foes
