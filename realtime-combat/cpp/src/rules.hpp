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

    // Circling while locked on. A cat can't walk sideways, so with a lock on, the player's left or right walks it forward
    // round the target: the game moves your cat along the camera's right (or left) and turns it to face the way it
    // walks, so with the camera on the line from your cat to the target, right is the circle's way round and your cat
    // curves left as it goes. The camera's held exactly on that line while circling (lock_rotation's easing lags a few
    // degrees, which spirals your cat outward), turned a little so the walk keeps to the circle it started on, `r0` from
    // the target: drifted out, the walk's tilted in; in, out. `side` is +1 for right, -1 for left.
    struct CircleSettings
    {
        double side_input = 0.3;   // how much left or right counts as circling
        double gain = 40;          // degrees of tilt for the whole circle's size off (kept within max_tilt)
        double max_tilt = 25;
    };
    inline auto circle_yaw(Vec me, Vec target, double r0, int side, const CircleSettings& s = {}) -> double
    {
        const double r = flat_distance(me, target);
        const double off = (r - r0) / std::max(r0, 50.0);
        const double tilt = std::clamp(off * s.gain, -s.max_tilt, s.max_tilt);
        return yaw_to(me, target) - (side > 0 ? 1 : -1) * tilt;
    }
    // Whether the player's asking to circle (enough left or right, from move_input's forward and right).
    inline auto circling(double right, const CircleSettings& s = {}) -> int
    {
        if (right > s.side_input) return 1;
        if (right < -s.side_input) return -1;
        return 0;
    }
    // (The game walks a cat forward along its facing and turns it toward the way it's asked to go at about 80 degrees
    // a second, seen in game: from a standstill your cat swings onto the circle over the first second or so. Turning
    // it faster from here didn't hold: the game turns it its own way each frame.)

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
        bool wide;                  // a wide swing: lands on every animal in the cone; else on one (the target, or the nearest)
    };
    inline auto spec(Attack a) -> const AttackSpec&
    {
        static const AttackSpec light{"light", L"Gameplay.Combat.Attack.Universal.2", 1.0, 1.95, 1.5, 1.3, 190, 60, 12, 1.0, 150, 0, false};
        static const AttackSpec heavy{"heavy", L"Gameplay.Combat.Attack.Universal.3", 0.85, 2.1, 1.6, 1.0, 210, 70, 25, 1.9, 350, 0, true};
        static const AttackSpec lunge{"lunge", L"Gameplay.Combat.Attack.Universal.14", 1.0, 2.05, 1.55, 1.1, 230, 55, 20, 1.5, 250, 700, false};
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
    // Which of the animals in reach (`candidates`, nearest first) an attack lands on: all of them for a wide swing;
    // else one, the one your cat's locked on to when it's in reach, otherwise the nearest. Indices into candidates.
    inline auto struck(const AttackSpec& s, size_t candidates, std::optional<size_t> locked) -> std::vector<size_t>
    {
        std::vector<size_t> out;
        if (candidates == 0) return out;
        if (s.wide)
        {
            for (size_t i = 0; i < candidates; ++i) out.push_back(i);
            return out;
        }
        out.push_back(locked && *locked < candidates ? *locked : 0);
        return out;
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
        // (a Clan life cat's model is "cat", "cat kit" or "cat medicine": the kits and the medicine cats first)
        static const Kind kinds[] = {{L"cat kit", 20}, {L"cat medicine", 60}, {L"cat", 85},
                                     {L"mice", 30}, {L"mouse", 30}, {L"rat", 40}, {L"rabbit", 45}, {L"squirrel", 35},
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
        double distance = 150;   // cm it slides (dodge.hpp takes it from settings.json's dodge_distance_m)
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

    // ------------------------------------------------------------------------------------------- the animals fight back
    // An animal's attack is its own default fight animation (Gameplay.Combat.Attack.Default, 4.3 s like your cat's: it
    // strikes at about 1.6 to 1.9 s, seen in game), played from `from` to `to`, landing at `hit`: about 0.75 s of wind-up
    // to see it coming. What's the same for every animal is here; what differs by kind is its FoeProfile (below).
    struct FoeSpec
    {
        std::wstring_view tag = L"Gameplay.Combat.Attack.Default";
        double from = 1.0, to = 2.2, hit = 1.75, rate = 1.0;
        double reach = 170, cone = 60;
        double engage = 400;           // an animal this near your cat joins the fight (cm)
        double give_up_range = 2500;   // farther than this from your cat,
        double leash = 2000;           // or than this from where it joined the fight,
        double give_up_after = 3;      // for this long: it goes back to roaming
        double stagger = 0.5;          // a hit in its wind-up stops the attack; it reels this long
        double commit = 0.3;           // the last part of a wind-up it no longer turns to your cat (when to dodge)
        double quick = 0.45;           // a combo's second strike, or a counter, lands this long after it starts
        double combo_gap = 0.15;       // between a strike and a combo's second
        double punish = 0.7;           // after striking it stays open this long, then backs off
        double retreat = 0.5;          // backing off to its spacing takes this long
        double hop = 0.6;              // a hop aside takes this long,
        double hop_safe = 0.45;        // the first part of it out of reach of your cat's claws (a light attack lands in it)
        double posture_hit = 25;       // a guarded light hit fills its posture by this (a heavy one twice)
        double posture_regen = 15;     // posture lost again each second it isn't guarding
        double guard_chip = 0.2;       // the part of a hit's damage a guard lets through
        double broken_for = 2.0;       // its guard broken: it reels this long, open to a riposte
        double pace_turn = 2.5;        // pacing: after this long it may turn and pace the other way
        double flee_escape = 1800;     // fleeing prey this far from your cat has got away
    };
    inline auto duration(const FoeSpec& s) -> double { return (s.to - s.from) / s.rate; }
    // A cat's attack: the cats' quick claw (your cat's light attack, its timing measured the same), not an animal's
    // default fight animation.
    inline auto cat_foe_spec() -> const FoeSpec&
    {
        static const FoeSpec s = [] {
            FoeSpec f;
            f.tag = L"Gameplay.Combat.Attack.Universal.2";
            f.from = 1.0, f.to = 1.95, f.hit = 1.5, f.rate = 1.0;
            return f;
        }();
        return s;
    }
    // A Clan life cat's level by its rank (Clan life tags it "ClanLife.rank.<rank>"), and the model it fights as.
    inline auto cat_level(std::wstring_view rank) -> int
    {
        if (rank == L"kit") return 1;
        if (rank == L"apprentice" || rank == L"medicine_cat_apprentice") return 4;
        if (rank == L"medicine_cat" || rank == L"elder") return 6;
        if (rank == L"queen") return 8;
        if (rank == L"deputy") return 13;
        if (rank == L"leader") return 16;
        return 9;   // a warrior
    }
    inline auto cat_model(std::wstring_view rank) -> std::wstring
    {
        if (rank == L"kit") return L"cat kit";
        if (rank == L"medicine_cat" || rank == L"medicine_cat_apprentice") return L"cat medicine";
        return L"cat";
    }
    inline auto hit_at(const FoeSpec& s) -> double { return (s.hit - s.from) / s.rate; }

    // How an animal of a kind fights. They don't all rush in: each keeps its distance (`spacing`), pacing round your cat
    // while it waits for an opening, and only one attacks at a time. Some guard when your cat attacks them from in
    // front, and a guard can be broken by filling its posture (heavy attacks fill it twice as fast); some hop aside;
    // some hold their wind-up to bait an early dodge (a feint), strike twice, or strike back after a guarded hit. Prey
    // that's losing runs for it.
    struct FoeProfile
    {
        double spacing = 260;          // the ring it paces while it waits (cm from your cat)
        double attack_range = 150;     // it rushes in to this to strike
        double speed = 330;            // closing in (cm/s)
        double pace_speed = 110;       // pacing round your cat
        double cooldown_min = 2.2, cooldown_max = 4.0;   // between its attacks
        double guard = 0;              // the chance it guards when your cat attacks it from in front
        double dodge = 0;              // the chance it hops aside instead
        double feint = 0;              // the chance its wind-up is held a while before it strikes
        double feint_min = 0.3, feint_max = 0.8;
        double combo = 0;              // the chance of a second strike straight after the first
        double counter = 0;            // the chance it strikes back at once after guarding a hit
        double posture = 60;           // how much guarding it takes before its guard breaks
        double guard_for = 1.2;        // how long it holds a guard
        double flee_below = 0;         // prey: below this part of its health it runs off
    };
    inline auto profile(std::wstring_view model) -> FoeProfile
    {
        std::wstring m(model);
        for (auto& c : m) c = static_cast<wchar_t>(std::towlower(c));
        auto is = [&](const wchar_t* part) { return m.find(part) != std::wstring::npos; };
        FoeProfile p;
        if (is(L"cat kit"))
        {
            // a kit doesn't fight: it runs from the first blow
            p.speed = 300, p.cooldown_min = p.cooldown_max = 1e9, p.dodge = 0.3, p.posture = 20, p.flee_below = 2;
        }
        else if (is(L"cat medicine"))
        {
            // a medicine cat defends itself, weakly
            p.cooldown_min = 3.5, p.cooldown_max = 5.5, p.guard = 0.15, p.dodge = 0.35, p.feint = 0.05, p.posture = 40;
        }
        else if (is(L"cat"))
        {
            // a warrior trained to fight cats: guards, dodges, feints, strikes twice, strikes back
            p.spacing = 230, p.attack_range = 150, p.speed = 340, p.cooldown_min = 2.0, p.cooldown_max = 3.6;
            p.guard = 0.35, p.dodge = 0.25, p.feint = 0.25, p.combo = 0.25, p.counter = 0.25, p.posture = 70;
        }
        else if (is(L"rat"))
        {
            p.spacing = 220, p.attack_range = 130, p.speed = 360, p.cooldown_min = 2.0, p.cooldown_max = 3.6;
            p.guard = 0.1, p.dodge = 0.35, p.feint = 0.15, p.combo = 0.15, p.counter = 0.1, p.posture = 45;
        }
        else if (is(L"mice") || is(L"mouse"))
        {
            p.spacing = 220, p.attack_range = 130, p.speed = 360, p.cooldown_min = 2.0, p.cooldown_max = 3.6;
            p.dodge = 0.45, p.feint = 0.1, p.posture = 35, p.flee_below = 0.25;
        }
        else if (is(L"rabbit"))
        {
            p.speed = 380, p.guard = 0.1, p.dodge = 0.35, p.feint = 0.15, p.posture = 40, p.flee_below = 0.3;
        }
        else if (is(L"squirrel"))
        {
            p.spacing = 240, p.speed = 370, p.dodge = 0.45, p.feint = 0.15, p.posture = 35, p.flee_below = 0.25;
        }
        else if (is(L"crow"))
        {
            p.dodge = 0.4, p.feint = 0.2, p.combo = 0.15, p.posture = 45;
        }
        else if (is(L"bird"))
        {
            p.dodge = 0.4, p.feint = 0.2, p.posture = 35, p.flee_below = 0.3;
        }
        else if (is(L"snake") || is(L"adder"))
        {
            p.spacing = 240, p.speed = 300, p.dodge = 0.2, p.feint = 0.5, p.feint_min = 0.4, p.feint_max = 1.0;
            p.counter = 0.2, p.posture = 50;
        }
        else if (is(L"fox"))
        {
            p.spacing = 280, p.cooldown_min = 2.0, p.cooldown_max = 3.4, p.guard = 0.35, p.dodge = 0.15, p.feint = 0.25;
            p.combo = 0.3, p.counter = 0.3, p.posture = 75;
        }
        else if (is(L"badger"))
        {
            p.speed = 280, p.pace_speed = 90, p.cooldown_min = 2.4, p.cooldown_max = 4.2, p.guard = 0.55, p.feint = 0.35;
            p.combo = 0.35, p.counter = 0.35, p.posture = 110, p.guard_for = 1.6;
        }
        else if (is(L"dog"))
        {
            p.spacing = 280, p.guard = 0.3, p.dodge = 0.1, p.feint = 0.2, p.combo = 0.4, p.counter = 0.25, p.posture = 90;
        }
        else
        {
            p.guard = 0.2, p.dodge = 0.15, p.feint = 0.2, p.combo = 0.15, p.counter = 0.15;
        }
        return p;
    }

    // How hard an animal hits your cat: a base for its kind, grown 10% a level past the first, times a setting (a
    // rabbit at level 14 takes 23 of a cat's 178).
    inline auto base_damage(std::wstring_view model) -> double
    {
        std::wstring m(model);
        for (auto& c : m) c = static_cast<wchar_t>(std::towlower(c));
        struct Kind
        {
            const wchar_t* part;
            double damage;
        };
        static const Kind kinds[] = {{L"cat kit", 0}, {L"cat medicine", 5}, {L"cat", 14},
                                     {L"mice", 8}, {L"mouse", 8}, {L"rat", 9}, {L"rabbit", 10}, {L"squirrel", 9}, {L"bird", 9},
                                     {L"crow", 10}, {L"snake", 16}, {L"adder", 16}, {L"fox", 20}, {L"badger", 26}, {L"dog", 28}};
        for (const Kind& k : kinds)
            if (m.find(k.part) != std::wstring::npos) return k.damage;
        return 12;
    }
    inline auto enemy_damage(std::wstring_view model, int level, double multiplier = 1.0) -> double
    {
        return base_damage(model) * (1.0 + 0.1 * (std::clamp(level, 1, 60) - 1)) * multiplier;
    }

    // What a defeated animal puts on your Clan's fresh-kill pile: prey feeds the Clan (a mouse 3, a rabbit 5...), and a
    // fox or a badger isn't prey.
    inline auto fresh_kill_for(std::wstring_view model) -> double
    {
        std::wstring m(model);
        for (auto& c : m) c = static_cast<wchar_t>(std::towlower(c));
        struct Kind
        {
            const wchar_t* part;
            double food;
        };
        static const Kind kinds[] = {{L"mice", 3}, {L"mouse", 3}, {L"rat", 3}, {L"rabbit", 5}, {L"squirrel", 4}, {L"bird", 4},
                                     {L"crow", 4}, {L"snake", 2}, {L"adder", 2}};
        for (const Kind& k : kinds)
            if (m.find(k.part) != std::wstring::npos) return k.food;
        return 0;
    }

    // An animal fighting your cat, as a state that moves on with time:
    //   Approach: it comes toward your cat, to its spacing;
    //   Pace:     it paces round your cat at its spacing, watching, until it's its turn and its cooldown's done;
    //   WindUp:   it rushes in and attacks, landing at `strike_at` (held a while first if it's a feint);
    //   Recover:  it struck; it's open for a moment (or strikes again: a combo);
    //   Retreat:  it backs off to its spacing;
    //   Guard:    it guards against your cat's attack (from in front: chip damage, and its posture fills);
    //   Hop:      it hops aside out of your cat's reach;
    //   Reel:     hit in its wind-up, parried, or its guard broken;
    //   Flee:     prey that's losing runs off.
    enum class FoeState
    {
        Approach,
        Pace,
        WindUp,
        Recover,
        Retreat,
        Guard,
        Hop,
        Reel,
        Flee,
    };
    constexpr auto code(FoeState s) -> std::string_view
    {
        switch (s)
        {
        case FoeState::Approach: return "approach";
        case FoeState::Pace: return "pace";
        case FoeState::WindUp: return "windup";
        case FoeState::Recover: return "recover";
        case FoeState::Retreat: return "retreat";
        case FoeState::Guard: return "guard";
        case FoeState::Hop: return "hop";
        case FoeState::Reel: return "reel";
        case FoeState::Flee: return "flee";
        }
        return "";
    }
    struct Foe
    {
        FoeState state = FoeState::Approach;
        double t = 0;          // seconds in this state
        double cooldown = 0;   // seconds until it may attack again
        double far_for = 0;    // seconds it's been past give_up_range or its leash
        double reel_for = 0;   // how long this reel lasts
        bool landed = false;
        double strike_at = 0;  // this wind-up lands this long after it starts
        double hold = 0;       // and is held this long first (a feint), from hold_at
        double hold_at = 0;
        bool held = false;     // the hold has begun,
        bool released = false; // and ended
        bool quick = false;    // this wind-up is a combo's second strike or a counter
        double posture = 0;    // filled by guarded hits; full, its guard breaks
        double guard_for = 0;
        int side = 1;          // which way round it paces (1: to your cat's left as it faces it, -1: right)
    };
    // What a step of time asks the caller to do.
    enum class FoeDo
    {
        Nothing,
        Approach,      // go toward your cat, to its spacing (follow at that distance)
        Pace,          // pace round your cat on its `side` (again when it turns)
        StartAttack,   // stop moving, face your cat, rush in to attack_range and play the attack from the start
        Hold,          // the feint: freeze the attack's animation where it is
        Release,       // and play it on
        Land,          // the hit: your cat in reach and in front, and not dodging, takes it
        Combo,         // a second strike: play the attack again from just before its strike (`quick` before it)
        Retreat,       // stop the attack's animation and back off to its spacing
        Flee,          // run away from your cat
        GiveUp,        // too far for too long, or got away: back to roaming
    };
    // The last part of a normal wind-up: from here it's committed.
    inline auto commit_at(const FoeSpec& s) -> double { return std::max(0.0, hit_at(s) - s.commit); }
    // Starts a wind-up: a normal one (with a feint's `hold`, 0 for none) or a quick one (a combo's second, a counter).
    inline auto wind_up(Foe& f, const FoeSpec& s, bool quick, double hold) -> void
    {
        f.state = FoeState::WindUp;
        f.t = 0;
        f.landed = false;
        f.quick = quick;
        f.hold = quick ? 0 : std::max(0.0, hold);
        f.hold_at = commit_at(s);
        f.held = false;
        f.released = false;
        f.strike_at = quick ? s.quick : hit_at(s) + f.hold;
    }
    // One step: `dt` seconds, your cat `distance` away, the animal `from_home` from where it joined the fight, `my_turn`
    // true when no other animal is attacking, `health` the part of its health it has left, and `roll()` a random number
    // in [0, 1) (the caller's; tests pass their own).
    template <typename Roll>
    auto step(Foe& f, const FoeSpec& s, const FoeProfile& p, double dt, double distance, double from_home, bool my_turn,
              double health, Roll&& roll) -> FoeDo
    {
        f.t += dt;
        f.cooldown = std::max(0.0, f.cooldown - dt);
        if (f.state != FoeState::Guard) f.posture = std::max(0.0, f.posture - s.posture_regen * dt);
        if (f.state == FoeState::Flee)
        {
            f.far_for = distance > s.flee_escape ? f.far_for + dt : 0;
            return f.far_for > 1.0 ? FoeDo::GiveUp : FoeDo::Nothing;
        }
        f.far_for = distance > s.give_up_range || from_home > s.leash ? f.far_for + dt : 0;
        if (f.far_for > s.give_up_after) return FoeDo::GiveUp;
        // prey that's losing runs for it, unless it's mid-strike or reeling
        if (p.flee_below > 0 && health < p.flee_below && f.state != FoeState::WindUp && f.state != FoeState::Reel)
        {
            f.state = FoeState::Flee;
            f.t = 0;
            return FoeDo::Flee;
        }
        auto next_cooldown = [&] { return p.cooldown_min + (p.cooldown_max - p.cooldown_min) * roll(); };
        auto to_pace = [&] {
            f.state = FoeState::Pace;
            f.t = 0;
            return FoeDo::Pace;
        };
        switch (f.state)
        {
        case FoeState::Approach:
            if (my_turn && f.cooldown <= 0 && distance <= p.spacing + 150)   // near enough to rush in
            {
                wind_up(f, s, false, roll() < p.feint ? p.feint_min + (p.feint_max - p.feint_min) * roll() : 0);
                return FoeDo::StartAttack;
            }
            if (distance <= p.spacing + 40)
            {
                f.side = roll() < 0.5 ? 1 : -1;
                return to_pace();
            }
            return FoeDo::Nothing;
        case FoeState::Pace:
            if (my_turn && f.cooldown <= 0)
            {
                wind_up(f, s, false, roll() < p.feint ? p.feint_min + (p.feint_max - p.feint_min) * roll() : 0);
                return FoeDo::StartAttack;
            }
            if (distance > p.spacing + 150)
            {
                f.state = FoeState::Approach;
                f.t = 0;
                return FoeDo::Approach;
            }
            if (f.t >= s.pace_turn)
            {
                f.t = 0;
                if (roll() < 0.4)
                {
                    f.side = -f.side;
                    return FoeDo::Pace;
                }
            }
            return FoeDo::Nothing;
        case FoeState::WindUp:
            if (f.hold > 0 && !f.held && f.t >= f.hold_at)
            {
                f.held = true;
                return FoeDo::Hold;
            }
            if (f.hold > 0 && f.held && !f.released && f.t >= f.hold_at + f.hold)
            {
                f.released = true;   // once, even after a long frame that passed the whole hold
                return FoeDo::Release;
            }
            if (!f.landed && f.t >= f.strike_at)
            {
                f.landed = true;
                f.state = FoeState::Recover;
                f.t = 0;
                return FoeDo::Land;
            }
            return FoeDo::Nothing;
        case FoeState::Recover:
            if (!f.quick && f.t >= s.combo_gap && f.t - dt < s.combo_gap && roll() < p.combo)
            {
                wind_up(f, s, true, 0);
                return FoeDo::Combo;
            }
            if (f.t >= s.punish)
            {
                f.state = FoeState::Retreat;
                f.t = 0;
                f.cooldown = next_cooldown();
                return FoeDo::Retreat;
            }
            return FoeDo::Nothing;
        case FoeState::Retreat:
            return f.t >= s.retreat ? to_pace() : FoeDo::Nothing;
        case FoeState::Guard:
            return f.t >= f.guard_for ? to_pace() : FoeDo::Nothing;
        case FoeState::Hop:
            return f.t >= s.hop ? to_pace() : FoeDo::Nothing;
        case FoeState::Reel:
            if (f.t >= f.reel_for)
            {
                f.state = FoeState::Retreat;
                f.t = 0;
                f.cooldown = std::max(f.cooldown, next_cooldown() * 0.5);
                return FoeDo::Retreat;
            }
            return FoeDo::Nothing;
        case FoeState::Flee: break;
        }
        return FoeDo::Nothing;
    }

    // Your cat starts an attack at it (it's in front of your cat and near): closing in, pacing or backing off, it may
    // guard or hop aside (`roll` in [0, 1)); already guarding, it keeps its guard up as long again (so a guard is worn
    // down by its posture, waited out, or got round). Mid-attack, open after striking, reeling or fleeing, it can't:
    // those are the times to hit it.
    enum class FoeReaction
    {
        None,
        Guard,
        KeepGuard,
        Hop,
    };
    inline auto react(Foe& f, const FoeProfile& p, double roll) -> FoeReaction
    {
        if (f.state == FoeState::Guard)
        {
            f.t = 0;
            return FoeReaction::KeepGuard;
        }
        if (f.state != FoeState::Approach && f.state != FoeState::Pace && f.state != FoeState::Retreat) return FoeReaction::None;
        if (roll < p.guard)
        {
            f.state = FoeState::Guard;
            f.t = 0;
            f.guard_for = p.guard_for;
            return FoeReaction::Guard;
        }
        if (roll < p.guard + p.dodge)
        {
            f.state = FoeState::Hop;
            f.t = 0;
            return FoeReaction::Hop;
        }
        return FoeReaction::None;
    }

    // Your cat's hit landing on it: from in front of it (`front`) or not, heavy or not.
    enum class FoeHit
    {
        Hit,        // the whole hit
        Staggered,  // in its wind-up: the whole hit, and its attack stops (it reels)
        Missed,     // it's in the safe part of a hop
        Blocked,    // guarded: guard_chip of it, and its posture fills
        Countered,  // guarded, and it strikes back at once (a quick wind-up)
        Broken,     // its posture full: its guard breaks, nothing gets through, and it reels, open to a riposte
    };
    constexpr auto code(FoeHit h) -> std::string_view
    {
        switch (h)
        {
        case FoeHit::Hit: return "hit";
        case FoeHit::Staggered: return "staggered";
        case FoeHit::Missed: return "missed";
        case FoeHit::Blocked: return "blocked";
        case FoeHit::Countered: return "countered";
        case FoeHit::Broken: return "broken";
        }
        return "";
    }
    struct FoeHitResult
    {
        FoeHit what = FoeHit::Hit;
        double part = 1;   // the part of the hit's damage it takes
    };
    inline auto take_hit(Foe& f, const FoeSpec& s, const FoeProfile& p, bool front, bool heavy, double roll) -> FoeHitResult
    {
        if (f.state == FoeState::Hop && f.t < s.hop_safe) return {FoeHit::Missed, 0};
        if (f.state == FoeState::Guard && front)
        {
            f.posture += s.posture_hit * (heavy ? 2 : 1);
            if (f.posture >= p.posture)
            {
                f.posture = 0;
                f.state = FoeState::Reel;
                f.t = 0;
                f.reel_for = s.broken_for;
                return {FoeHit::Broken, 0};
            }
            if (roll < p.counter)
            {
                wind_up(f, s, true, 0);
                return {FoeHit::Countered, s.guard_chip};
            }
            return {FoeHit::Blocked, s.guard_chip};
        }
        if (f.state == FoeState::WindUp && !f.landed)
        {
            f.state = FoeState::Reel;
            f.t = 0;
            f.reel_for = s.stagger;
            return {FoeHit::Staggered, 1};
        }
        return {FoeHit::Hit, 1};
    }
    // Your cat parried it: whatever it was doing, it reels `seconds`, open to a riposte.
    inline auto parried(Foe& f, double seconds) -> void
    {
        f.state = FoeState::Reel;
        f.t = 0;
        f.reel_for = seconds;
        f.landed = true;
    }
    // Whether it holds the turn to attack (only one animal attacks at a time; the others pace and wait).
    inline auto attacking(const Foe& f) -> bool { return f.state == FoeState::WindUp || f.state == FoeState::Recover; }

    // ------------------------------------------------------------------------------------------- blocking and parrying
    // Hold the block key: your cat crouches in a guard (a stand-in: its Stone Guard fight move, held at 1.0 s, its low
    // crouch). A hit landing from in front (within `front` degrees of its facing) while it guards does `chip` of its
    // damage, and its damage comes off stamina instead; with too little stamina, the guard breaks (your cat's open and
    // reels `break_for` seconds, taking the chip). A guard begun just before the hit lands (within `parry_window`) is a
    // parry (a stand-in: Hissing Parry, 0.9 s to 1.6 s): no damage, and the animal reels `parry_reel` seconds, open to a
    // riposte: your cat's next hit on it in that time does `riposte` times its damage. (A guard raised again within half a
    // second of dropping can't parry: guard.hpp.)
    struct GuardSpec
    {
        std::wstring_view guard_tag = L"Gameplay.Combat.Attack.RiverClan.3";
        double guard_pose = 1.0;
        std::wstring_view parry_tag = L"Gameplay.Combat.Attack.RiverClan.6";
        double parry_from = 0.9, parry_to = 1.6, parry_rate = 1.3;
        double front = 75;
        double chip = 0.25;
        double stamina_per_damage = 1.0;
        double break_for = 1.0;
        double parry_window = 0.2;
        double parry_reel = 2.0;
        double riposte = 3.0;
    };
    enum class Guarded
    {
        Hit,          // not guarding, or the hit came from behind
        Blocked,
        GuardBroken,
        Parried,
    };
    constexpr auto code(Guarded g) -> std::string_view
    {
        switch (g)
        {
        case Guarded::Hit: return "hit";
        case Guarded::Blocked: return "blocked";
        case Guarded::GuardBroken: return "guard-broken";
        case Guarded::Parried: return "parried";
        }
        return "";
    }
    struct GuardResult
    {
        Guarded what = Guarded::Hit;
        double health = 0;    // what comes off your cat's health
        double stamina = 0;   // and off its stamina
    };
    // A hit of `damage` landing on your cat: guarding for `held_for` seconds (or not), the attacker in front of it or
    // not, with `stamina` left.
    inline auto guard(bool guarding, double held_for, bool in_front, double stamina, double damage, const GuardSpec& s = {})
        -> GuardResult
    {
        if (!guarding || !in_front) return {Guarded::Hit, damage, 0};
        if (held_for <= s.parry_window) return {Guarded::Parried, 0, 0};
        const double cost = damage * s.stamina_per_damage;
        if (stamina >= cost) return {Guarded::Blocked, damage * s.chip, cost};
        return {Guarded::GuardBroken, damage * s.chip, stamina};
    }
    inline auto parry_duration(const GuardSpec& s) -> double { return (s.parry_to - s.parry_from) / s.parry_rate; }

    // ------------------------------------------------------------------------------------------- the flask
    // A drink: the game has no drinking animation, so its sniff (head down, as if lapping) stands in, played from `from`
    // to `to` at `rate`; health comes back over `heal_from` to `heal_to` (real seconds from the press), `heal` of the
    // most your cat can have in all. A hit while drinking stops it, the charge spent.
    struct FlaskSpec
    {
        std::wstring_view tag = L"Animation.Exploration.Interaction.Sniff";
        double from = 0.4, to = 2.0, rate = 1.4;
        double heal_from = 0.45, heal_to = 1.05;
        double heal = 0.4;
        int charges = 3;
    };
    inline auto duration(const FlaskSpec& s) -> double { return (s.to - s.from) / s.rate; }
    // How much comes back between `before` and `after` seconds into a drink: the part of the healing time inside them.
    inline auto healed_between(const FlaskSpec& s, double max_health, double before, double after) -> double
    {
        const double a = std::max(before, s.heal_from), b = std::min(after, s.heal_to);
        if (b <= a || s.heal_to <= s.heal_from) return 0;
        return max_health * s.heal * (b - a) / (s.heal_to - s.heal_from);
    }
    // The flask fills again when your cat rests: comes into the forest, sleeps, or goes back to camp (the game's
    // Return to Camp, or any travel, moves it farther in a frame than any running could: `jump` cm).
    constexpr double travel_jump = 3000;
    inline auto travelled(Vec before, Vec after) -> bool { return flat_distance(before, after) > travel_jump; }
} // namespace rtc
