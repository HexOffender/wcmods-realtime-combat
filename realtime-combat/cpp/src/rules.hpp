// rules.hpp: the mod's rules as plain C++, with nothing of the game or UE4SS in it, so tests/unit can build and run
// them anywhere. The game-facing code (main.cpp) gathers what the game says and asks these.
#pragma once

#include <string_view>

namespace rtc
{
    // ------------------------------------------------------------------------------------------- the Sandbox gate
    // What decides whether real-time combat is on: it's only for a Sandbox mode cat, out in the world.
    struct GateInput
    {
        bool enabled = true;             // settings.json's "enabled" (the mod menu's switch)
        bool sandbox_installed = false;  // the Sandbox mode mod shares its version with C++ mods (0.7 and later)
        bool sandbox_ready = false;      // a sandbox cat is being played: in the forest, the game begun, no story scene
        bool in_world = false;           // your cat is in the world (not a menu, not a loading screen)
        bool in_fight = false;           // a turn-based fight is on (one that started before the mod came on)
    };

    // Why it's off; None when it's on. In the order they're checked.
    enum class Off
    {
        None,
        Disabled,
        NoSandbox,
        NotSandbox,
        InFight,
        NotInWorld,
    };

    // A fight is looked at before the world: in one, the game's player() is the fight's pawn's (no cat in the world),
    // so "not in the world" would hide it.
    constexpr auto gate(const GateInput& in) -> Off
    {
        if (!in.enabled) return Off::Disabled;
        if (!in.sandbox_installed) return Off::NoSandbox;
        if (!in.sandbox_ready) return Off::NotSandbox;
        if (in.in_fight) return Off::InFight;
        if (!in.in_world) return Off::NotInWorld;
        return Off::None;
    }

    // Said on the mod menu's page and in the log.
    constexpr auto why(Off off) -> std::wstring_view
    {
        switch (off)
        {
        case Off::None: return L"on";
        case Off::Disabled: return L"switched off in its settings";
        case Off::NoSandbox: return L"the Sandbox mode mod (0.7 or later) isn't running";
        case Off::NotSandbox: return L"not a Sandbox mode cat";
        case Off::NotInWorld: return L"your cat isn't out in the world";
        case Off::InFight: return L"a turn-based fight is on";
        }
        return L"";
    }

    // The same as a short word, shared for tests and other mods to read.
    constexpr auto code(Off off) -> std::string_view
    {
        switch (off)
        {
        case Off::None: return "on";
        case Off::Disabled: return "disabled";
        case Off::NoSandbox: return "no-sandbox";
        case Off::NotSandbox: return "not-sandbox";
        case Off::NotInWorld: return "not-in-world";
        case Off::InFight: return "in-fight";
        }
        return "";
    }
} // namespace rtc
