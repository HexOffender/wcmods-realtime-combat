// rules.hpp: the mod's rules as plain C++, with nothing of the game or UE4SS in it, so tests/unit can build and run
// them anywhere. The game-facing code (main.cpp) gathers what the game says and asks these.
#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

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

    // ------------------------------------------------------------------------------------------- places and turns
    // The game's units: centimetres, and Unreal's yaw (degrees, 0 along +X, 90 along +Y: turning right seen from
    // above) and pitch (degrees, below 0 looks down).
    struct Vec
    {
        double x = 0, y = 0, z = 0;
    };
    struct Rot
    {
        double pitch = 0, yaw = 0;
    };

    inline auto flat_distance(Vec a, Vec b) -> double { return std::hypot(b.x - a.x, b.y - a.y); }
    inline auto distance(Vec a, Vec b) -> double { return std::hypot(b.x - a.x, b.y - a.y, b.z - a.z); }

    constexpr double degrees_per_radian = 180.0 / 3.14159265358979323846;

    // The yaw that looks from one place to another, and the pitch.
    inline auto yaw_to(Vec from, Vec to) -> double { return std::atan2(to.y - from.y, to.x - from.x) * degrees_per_radian; }
    inline auto pitch_to(Vec from, Vec to) -> double
    {
        return std::atan2(to.z - from.z, flat_distance(from, to)) * degrees_per_radian;
    }

    // How far to turn from yaw `from` to yaw `to`, the short way: -180 (exclusive) to 180, positive to the right.
    inline auto turn(double from, double to) -> double
    {
        double d = std::fmod(to - from, 360.0);
        if (d <= -180.0) d += 360.0;
        if (d > 180.0) d -= 360.0;
        return d;
    }

    // ------------------------------------------------------------------------------------------- lock-on
    struct LockSettings
    {
        double range = 1500;          // how far an animal can be to be locked on to (cm)
        double release_range = 2200;  // a locked animal farther than this is let go
        double max_angle = 75;        // how far from the middle of the view (degrees of yaw) one can be picked
        double unseen_release = 2.0;  // seconds out of sight before it's let go
        double pitch = -12;           // the camera's pitch over the line to the target: a little down
        double pitch_min = -45, pitch_max = 15;
        double turn_speed = 8;        // how quickly the camera turns to the target (higher is snappier)
    };

    // An animal that could be locked on to: an id of the caller's (its address), where it is, and whether your
    // cat can see it.
    struct Candidate
    {
        unsigned long long id = 0;
        Vec at;
        bool seen = true;
    };

    // The animal to lock on to as the key's pressed: of those seen and within range, the one nearest the middle of
    // the view (ten degrees count as much as the whole range, so of two nearly in line the nearer wins). -1 for none.
    inline auto pick_target(Vec me, double view_yaw, const std::vector<Candidate>& all, const LockSettings& s = {}) -> int
    {
        int best = -1;
        double best_score = 0;
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
        {
            const Candidate& c = all[i];
            const double d = distance(me, c.at);
            if (!c.seen || d > s.range) continue;
            const double angle = std::abs(turn(view_yaw, yaw_to(me, c.at)));
            if (angle > s.max_angle) continue;
            const double score = angle + 10.0 * d / s.range;
            if (best < 0 || score < best_score)
            {
                best = i;
                best_score = score;
            }
        }
        return best;
    }

    // The next animal to one side of the one locked on (side > 0: to the right, < 0: to the left), as seen from your
    // cat: of those seen and within range, the least turn that way. -1 when there's none that way.
    inline auto next_target(Vec me, Vec current, unsigned long long current_id, int side, const std::vector<Candidate>& all,
                            const LockSettings& s = {}) -> int
    {
        const double now = yaw_to(me, current);
        int best = -1;
        double best_turn = 0;
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
        {
            const Candidate& c = all[i];
            if (c.id == current_id || !c.seen || distance(me, c.at) > s.range) continue;
            const double t = turn(now, yaw_to(me, c.at)) * (side > 0 ? 1 : -1);
            if (t <= 0) continue;
            if (best < 0 || t < best_turn)
            {
                best = i;
                best_turn = t;
            }
        }
        return best;
    }

    // Whether a lock holds, and why not.
    enum class Release
    {
        Keep,
        Gone,    // the animal's gone (taken out of the world, or it fell)
        TooFar,  // it's farther than release_range
        Unseen,  // out of sight for longer than unseen_release
    };
    inline auto keep_lock(bool gone, double distance_now, double unseen_for, const LockSettings& s = {}) -> Release
    {
        if (gone) return Release::Gone;
        if (distance_now > s.release_range) return Release::TooFar;
        if (unseen_for > s.unseen_release) return Release::Unseen;
        return Release::Keep;
    }
    constexpr auto code(Release r) -> std::string_view
    {
        switch (r)
        {
        case Release::Keep: return "keep";
        case Release::Gone: return "gone";
        case Release::TooFar: return "too-far";
        case Release::Unseen: return "unseen";
        }
        return "";
    }

    // Where the camera looks while locked on: from `now` toward the target, eased (`dt` seconds since the last
    // frame), its pitch a little below the line from your cat to the target and kept in bounds. Unreal hands a
    // pitch back as 0 to 360; it's taken as -180 to 180.
    inline auto lock_rotation(Rot now, Vec me, Vec target, double dt, const LockSettings& s = {}) -> Rot
    {
        const double want_yaw = yaw_to(me, target);
        const double want_pitch = std::clamp(pitch_to(me, target) + s.pitch, s.pitch_min, s.pitch_max);
        const double k = 1.0 - std::exp(-s.turn_speed * std::max(dt, 0.0));
        const double pitch_now = now.pitch > 180 ? now.pitch - 360 : now.pitch;
        Rot out;
        out.yaw = now.yaw + turn(now.yaw, want_yaw) * k;
        out.pitch = pitch_now + (want_pitch - pitch_now) * k;
        return out;
    }
} // namespace rtc
