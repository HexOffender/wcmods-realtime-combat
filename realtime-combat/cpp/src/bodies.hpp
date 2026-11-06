// bodies.hpp: the animals' bodies while real-time combat is on. In the game a roaming animal's body overlaps cats rather
// than blocking them, and each has an ExplorationEncounterCollision capsule that starts the game's own turn-based
// fight when a cat overlaps it, whether or not the animal has noticed the cat (seen in game: your cat slid into a
// rabbit that ignored it, and a fight started). So while it's on, for each animal within 5,000 units (a look twice a
// second): its encounter capsule ignores cats, and its body blocks them, so your cat bumps into it instead. Off, each
// is put back as it was.
//
// Shared for tests: "bodies" (how many animals are set so).
#pragma once

#include <WCMods/WCMods.hpp>

#include <map>

#include "game.hpp"

namespace rtc::bodies
{
    using wcmods::UObject;

    constexpr int pawn_channel = 2;   // ECC_Pawn: cats and animals are pawns
    constexpr int ignore = 0, overlap = 1, block = 2;

    struct Kept
    {
        UObject* encounter{};
        UObject* body{};
        int encounter_was = overlap, body_was = overlap;
    };
    struct State
    {
        std::map<UObject*, Kept> set;
        double next = 0;
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    inline auto response(UObject* component) -> std::optional<int>
    {
        auto r = wcmods::call_reflected(component, STR("GetCollisionResponseToChannel"),
                                        [&](wcmods::Params& p) { p.set_nth(0, wcmods::Value::of(pawn_channel)); });
        if (!r) return std::nullopt;
        return static_cast<int>(r->get(L"ReturnValue").number());
    }
    inline auto set_response(UObject* component, int to) -> bool
    {
        return wcmods::call_reflected(component, STR("SetCollisionResponseToChannel"), [&](wcmods::Params& p) {
                   p.set_nth(0, wcmods::Value::of(pawn_channel));
                   p.set_nth(1, wcmods::Value::of(to));
               }).has_value();
    }

    // One animal set: its encounter capsule ignoring cats, its body blocking them; what they were kept.
    inline auto take(UObject* animal) -> void
    {
        Kept k;
        k.encounter = wcmods::get(animal, L"ExplorationEncounterCollision").object;
        k.body = wcmods::get(animal, L"CapsuleComponent").object;
        if (k.encounter)
            if (auto was = response(k.encounter)) k.encounter_was = *was;
        if (k.body)
            if (auto was = response(k.body)) k.body_was = *was;
        if (k.encounter) set_response(k.encounter, ignore);
        if (k.body) set_response(k.body, block);
        state().set[animal] = k;
    }

    inline auto give_back(UObject* animal, const Kept& k) -> void
    {
        if (!game::staying(animal)) return;
        if (k.encounter && game::staying(k.encounter)) set_response(k.encounter, k.encounter_was);
        if (k.body && game::staying(k.body)) set_response(k.body, k.body_was);
    }

    // Every frame while real-time combat is on: twice a second, the animals that have come near are set.
    inline auto frame(double dt) -> void
    {
        State& s = state();
        s.next -= dt;
        if (s.next > 0) return;
        s.next = 0.5;
        std::erase_if(s.set, [](auto& e) { return !game::staying(e.first); });
        for (const wcmods::Animal& a : wcmods::animals(5000))
            if (!s.set.contains(a.actor)) take(a.actor);
        wcmods::share("bodies", static_cast<double>(s.set.size()));
    }

    // Real-time combat off: every animal set is as it was.
    inline auto clear() -> void
    {
        State& s = state();
        for (auto& [animal, k] : s.set) give_back(animal, k);
        s.set.clear();
        s.next = 0;
        wcmods::share("bodies", 0);
    }
} // namespace rtc::bodies
