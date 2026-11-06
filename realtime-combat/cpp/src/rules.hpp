// rules.hpp: the mod's rules as plain C++, with nothing of the game or UE4SS in it, so tests/unit can build and run
// them anywhere. The game-facing code (main.cpp) gathers what the game says and asks these.
#pragma once

#include <algorithm>
#include <cwctype>
#include <cmath>
#include <optional>
#include <string>
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

    // ------------------------------------------------------------------------------------------- attacks
    // Your cat's attacks are the fights' own animations, which are 4.3 s long: the cat crouches, runs in (the fights
    // warp it to its target), strikes at about 1.5 s and walks back. A real-time attack plays only the strike, from
    // `from` to `to` in the montage's own seconds, at `rate`; the hit lands at `hit` (seen in game: Quick Claw crouched
    // at 1.0 s, its paw out at 1.5 s, upright again by 2.2 s).
    enum class Attack
    {
        Light,
        Heavy,
        Lunge,
    };
    struct AttackSpec
    {
        std::string_view name;
        std::wstring_view tag;      // the row of the cat's animation table
        double from, to, hit, rate; // montage seconds, and its speed
        double reach;               // how far in front a hit reaches, from your cat's middle (cm)
        double cone;                // how wide, in degrees either side of your cat's facing
        double stamina;             // what it costs
        double damage;              // times the light attack's damage
        double push;                // how hard it pushes what it hits away (cm/s)
        double lunge;               // how fast it carries your cat forward as it strikes, for 0.3 s (cm/s, 0 for none)
    };
    inline auto spec(Attack a) -> const AttackSpec&
    {
        static const AttackSpec light{"light", L"Gameplay.Combat.Attack.Universal.2", 1.0, 1.95, 1.5, 1.3, 190, 60, 12, 1.0, 150, 0};
        static const AttackSpec heavy{"heavy", L"Gameplay.Combat.Attack.Universal.3", 0.85, 2.1, 1.6, 1.0, 210, 70, 25, 1.9, 350, 0};
        static const AttackSpec lunge{"lunge", L"Gameplay.Combat.Attack.Universal.14", 1.0, 2.05, 1.55, 1.1, 230, 55, 20, 1.5, 250, 700};
        switch (a)
        {
        case Attack::Light: return light;
        case Attack::Heavy: return heavy;
        case Attack::Lunge: return lunge;
        }
        return light;
    }
    // In real seconds from the press: how long it lasts, when it lands, and from when another light attack pressed
    // is kept to follow it (once it's landed).
    inline auto duration(const AttackSpec& s) -> double { return (s.to - s.from) / s.rate; }
    inline auto hit_at(const AttackSpec& s) -> double { return (s.hit - s.from) / s.rate; }
    inline auto chain_from(const AttackSpec& s) -> double { return hit_at(s); }

    // Stamina, as the game keeps it (100 on a cat, 20 a second back): an attack needs at least half its cost, and takes
    // it to 0 at worst; none comes back while it plays or for `stamina_pause` seconds after.
    inline auto can_spend(double stamina, double cost) -> bool { return stamina >= cost * 0.5; }
    inline auto spend(double stamina, double cost) -> double { return std::max(0.0, stamina - cost); }
    constexpr double stamina_pause = 0.6;

    // Whether a place is within an attack's reach: close enough (flat distance) and in the cone in front, and not far
    // above or below.
    inline auto in_reach(Vec me, double facing_yaw, Vec them, double reach, double cone) -> bool
    {
        if (flat_distance(me, them) > reach || std::abs(them.z - me.z) > 150) return false;
        if (flat_distance(me, them) < 40) return true;   // right on top of your cat
        return std::abs(turn(facing_yaw, yaw_to(me, them))) <= cone;
    }

    // Your cat's damage: the light attack's grows with its level (23 at level 14), the others are multiples of it.
    inline auto cat_damage(Attack a, int level, double multiplier = 1.0) -> double
    {
        return (8.0 + 1.1 * std::clamp(level, 1, 60)) * spec(a).damage * multiplier;
    }

    // ------------------------------------------------------------------------------------------- enemies
    // An animal's health in a real-time fight: a base for its kind, by its mesh's name (SK_Mice, SK_Rabbit...), grown
    // 12% a level past the first. Its level comes from its kind's tag when a mod spawned it ("...Rabbit.01.L6"), else
    // the caller's guess (your cat's level).
    inline auto base_health(std::wstring_view model) -> double
    {
        std::wstring m(model);
        for (auto& c : m) c = static_cast<wchar_t>(std::towlower(c));
        struct Kind
        {
            const wchar_t* part;
            double health;
        };
        static const Kind kinds[] = {{L"mice", 30}, {L"mouse", 30}, {L"rat", 40}, {L"rabbit", 45}, {L"squirrel", 35},
                                     {L"bird", 35}, {L"crow", 40}, {L"snake", 70}, {L"adder", 70}, {L"fox", 110},
                                     {L"badger", 150}, {L"dog", 160}};
        for (const Kind& k : kinds)
            if (m.find(k.part) != std::wstring::npos) return k.health;
        return 50;
    }
    inline auto enemy_health(std::wstring_view model, int level, double multiplier = 1.0) -> double
    {
        return base_health(model) * (1.0 + 0.12 * (std::clamp(level, 1, 60) - 1)) * multiplier;
    }
    // "Gameplay.Combat.Generator.Fixed.Rabbit.01.L6" -> 6; nullopt without an ".L<number>" at its end.
    inline auto level_of_kind(std::wstring_view kind) -> std::optional<int>
    {
        const auto dot = kind.rfind(L".L");
        if (dot == std::wstring_view::npos || dot + 2 >= kind.size()) return std::nullopt;
        int n = 0;
        for (size_t i = dot + 2; i < kind.size(); ++i)
        {
            if (kind[i] < L'0' || kind[i] > L'9') return std::nullopt;
            n = n * 10 + (kind[i] - L'0');
            if (n > 999) return std::nullopt;
        }
        return n;
    }
    // What's left after a hit, and whether that was the last of it.
    inline auto after_hit(double health, double damage) -> double { return std::max(0.0, health - std::max(0.0, damage)); }

    // ------------------------------------------------------------------------------------------- the dodge
    // The game has a hop to each side (Animation.Combat.Dodge.Left/Right, 2 s each): crouched to about 0.3 s, the hop
    // from about 0.4 s to 0.8 s, landed by 1.0 s (seen in game). A dodge plays from `from` to `to` at `rate`, slides your
    // cat `distance` cm while the hop's in the air (slide_from to slide_to, montage seconds), and nothing hurts it for
    // its first `safe` real seconds.
    struct DodgeSpec
    {
        double from = 0.25, to = 1.15, rate = 1.4;
        double slide_from = 0.35, slide_to = 0.85;
        double distance = 300;
        double safe = 0.4;
        double stamina = 15;
    };
    inline auto duration(const DodgeSpec& s) -> double { return (s.to - s.from) / s.rate; }
    // Real seconds from the press when the slide starts and ends.
    inline auto slide_start(const DodgeSpec& s) -> double { return std::max(0.0, s.slide_from - s.from) / s.rate; }
    inline auto slide_end(const DodgeSpec& s) -> double { return (s.slide_to - s.from) / s.rate; }
    inline auto slide_speed(const DodgeSpec& s) -> double { return s.distance / std::max(0.01, slide_end(s) - slide_start(s)); }
    inline auto safe_at(const DodgeSpec& s, double t) -> bool { return t >= 0 && t < s.safe; }

    // Which way it goes: the way the player's moving (forward and right from -1 to 1, as the camera sees them), or
    // straight back from where your cat faces when they aren't.
    inline auto dodge_yaw(double camera_yaw, double cat_yaw, double forward, double right) -> double
    {
        if (std::hypot(forward, right) < 0.2) return cat_yaw + 180;
        return camera_yaw + std::atan2(right, forward) * degrees_per_radian;
    }

    // How it's played: a hop to the side the dodge goes, with your cat facing as it does when that's to its side;
    // forward or back, your cat's turned so the hop to its right goes that way.
    struct DodgePose
    {
        bool right = true;
        double cat_yaw = 0;
    };
    inline auto dodge_pose(double cat_yaw, double yaw) -> DodgePose
    {
        const double a = turn(cat_yaw, yaw);
        if (a >= 30 && a <= 150) return {true, cat_yaw};
        if (a <= -30 && a >= -150) return {false, cat_yaw};
        return {true, yaw - 90};
    }

    // A dodge can cut an attack short once it's landed, not in its wind-up.
    inline auto can_dodge_out_of(const AttackSpec& a, double elapsed) -> bool { return elapsed >= hit_at(a); }
} // namespace rtc
