// input.hpp: the player's actions, from the keys in settings.json's "keys" (Unreal's key names, read through the game's
// own input, so the mouse's buttons and wheel and a controller's buttons work too), and from tests: a test shares
// "RealtimeCombat.action" from Lua (WCModsCpp.share("RealtimeCombat.action", "lock 3")), and each new value is the
// action before the space, done once.
#pragma once

#include <WCMods/WCMods.hpp>

#include <array>
#include <mutex>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace rtc::input
{
    enum class Action
    {
        Lock,
        NextRight,
        NextLeft,
        Light,
        Heavy,
        Lunge,
        Dodge,
        Drink,
        Block,
        Finish,   // the killing blow on a cat that's yielded (cats.hpp)
        Hurt,   // tests only: your cat takes a small hit, as an animal's would
        Count,
    };

    struct Binding
    {
        Action action;
        const char* setting;                    // its name in settings.json's "keys"
        const char* word;                       // its name from tests
        std::vector<std::wstring> defaults;     // the keys it's on unless the settings say
    };

    inline auto bindings() -> const std::vector<Binding>&
    {
        static const std::vector<Binding> all{
            // a controller's, beside the keyboard's: the buttons the game leaves free in play where there are any (the
            // right stick's click, the right trigger, the d-pad's up, left and right); the heavy attack, the block and
            // the dodge share the game's crouch (LT), camera reset (LB) and drop / talk to a friend (B), as the block
            // shares the right mouse button's crouch
            {Action::Lock, "lock", "lock", {L"Tab", L"MiddleMouseButton", L"Gamepad_RightThumbstick"}},
            {Action::NextRight, "next_right", "next_right", {L"MouseScrollUp", L"Gamepad_DPad_Right"}},
            {Action::NextLeft, "next_left", "next_left", {L"MouseScrollDown", L"Gamepad_DPad_Left"}},
            {Action::Light, "light_attack", "light", {L"F", L"Gamepad_RightTrigger"}},
            {Action::Heavy, "heavy_attack", "heavy", {L"G", L"Gamepad_LeftTrigger"}},
            {Action::Lunge, "lunge", "lunge", {}},   // the light attack while running; a key of its own if the settings give one
            {Action::Dodge, "dodge", "dodge", {L"LeftAlt", L"Gamepad_FaceButton_Right"}},
            {Action::Drink, "drink", "drink", {L"Z", L"Gamepad_DPad_Up"}},
            {Action::Block, "block", "block", {L"RightMouseButton", L"Gamepad_LeftShoulder"}},   // held (held(), below)
            {Action::Finish, "finish", "finish", {L"H", L"Gamepad_DPad_Down"}},
            {Action::Hurt, nullptr, "hurt", {}},   // tests only: no key can be put on it
        };
        return all;
    }

    struct State
    {
        std::array<std::vector<std::wstring>, static_cast<size_t>(Action::Count)> keys;
        std::string last_test;
    };
    inline auto state() -> State&
    {
        static State s;
        return s;
    }

    // The keys each action is on: settings.json's "keys" (a key's name, or a list of them), else the defaults.
    inline auto load() -> void
    {
        State& s = state();
        std::lock_guard lock(wcmods::settings_lock());
        const wcmods::Json& keys = wcmods::settings()["keys"];
        for (const Binding& b : bindings())
        {
            auto& out = s.keys[static_cast<size_t>(b.action)];
            out.clear();
            if (!b.setting) continue;
            const wcmods::Json& v = keys[b.setting];
            if (v.is_string()) out.push_back(v.wstring());
            else if (v.is_array())
                for (const wcmods::Json& k : v.values())
                    if (k.is_string() && !k.wstring().empty()) out.push_back(k.wstring());
            if (out.empty() && !v.is_array()) out = b.defaults;   // [] is "on no key"
        }
    }

    // What the player asked for this frame.
    struct Pressed
    {
        std::array<bool, static_cast<size_t>(Action::Count)> on{};
        std::optional<double> dodge_way;   // tests only: the dodge's way, in degrees right of the camera's
        auto operator[](Action a) const -> bool { return on[static_cast<size_t>(a)]; }
    };

    inline auto poll() -> Pressed
    {
        State& s = state();
        Pressed p;
        for (const Binding& b : bindings())
            for (const std::wstring& key : s.keys[static_cast<size_t>(b.action)])
                if (wcmods::pressed(key)) p.on[static_cast<size_t>(b.action)] = true;   // (none for the tests' own)
        const std::string test = wcmods::shared("RealtimeCombat.action").string();
        if (!test.empty() && test != s.last_test)
        {
            s.last_test = test;
            const std::string word = test.substr(0, test.find(' '));
            for (const Binding& b : bindings())
                if (word == b.word) p.on[static_cast<size_t>(b.action)] = true;
            // "dodge_left" and the like: a dodge that way, whatever keys are held
            static const std::pair<const char*, double> ways[] = {{"dodge_forward", 0}, {"dodge_right", 90}, {"dodge_back", 180}, {"dodge_left", -90}};
            for (auto& [w, way] : ways)
                if (word == w)
                {
                    p.on[static_cast<size_t>(Action::Dodge)] = true;
                    p.dodge_way = way;
                }
        }
        return p;
    }

    // Whether one of an action's keys is held now (the block). Tests say so with "RealtimeCombat.block" shared true.
    inline auto held(Action a) -> bool
    {
        if (wcmods::shared("RealtimeCombat.block").boolean(false) && a == Action::Block) return true;
        for (const std::wstring& key : state().keys[static_cast<size_t>(a)])
            if (wcmods::key_down(key)) return true;
        return false;
    }
    // The keys an action is on, for the mod menu: "Tab, MiddleMouseButton".
    inline auto keys_of(Action a) -> std::wstring
    {
        std::wstring out;
        for (const std::wstring& k : state().keys[static_cast<size_t>(a)]) out += (out.empty() ? L"" : L", ") + k;
        return out.empty() ? L"(none)" : out;
    }
} // namespace rtc::input
