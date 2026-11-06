// Unit tests for realtime-combat/cpp/src/rules.hpp: plain C++20, no game, no UE4SS. tests/unit/run.py builds and runs
// them (g++, clang++ or Visual Studio's cl), and so does GitHub's CI.
#include "rules.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int failures = 0;
    int checks = 0;

    void check(bool ok, const char* what, int line)
    {
        ++checks;
        if (ok) return;
        ++failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
} // namespace

#define CHECK(x) check((x), #x, __LINE__)

static void gate_is_on_only_for_a_sandbox_cat_in_the_world()
{
    rtc::GateInput in{.enabled = true, .sandbox_installed = true, .sandbox_ready = true, .in_world = true, .in_fight = false};
    CHECK(rtc::gate(in) == rtc::Off::None);

    auto off = in;
    off.enabled = false;
    CHECK(rtc::gate(off) == rtc::Off::Disabled);

    off = in;
    off.sandbox_installed = false;
    CHECK(rtc::gate(off) == rtc::Off::NoSandbox);

    off = in;
    off.sandbox_ready = false;
    CHECK(rtc::gate(off) == rtc::Off::NotSandbox);

    off = in;
    off.in_world = false;
    CHECK(rtc::gate(off) == rtc::Off::NotInWorld);

    off = in;
    off.in_fight = true;
    CHECK(rtc::gate(off) == rtc::Off::InFight);
}

static void gate_says_the_first_reason()
{
    // Switched off wins over everything; no Sandbox mode over not a sandbox cat.
    rtc::GateInput none{};
    none.enabled = false;
    CHECK(rtc::gate(none) == rtc::Off::Disabled);
    none.enabled = true;
    CHECK(rtc::gate(none) == rtc::Off::NoSandbox);
    none.sandbox_installed = true;
    CHECK(rtc::gate(none) == rtc::Off::NotSandbox);

    // In a fight the game has no cat in the world (player() is the fight's): the fight is what's said.
    rtc::GateInput fight{.enabled = true, .sandbox_installed = true, .sandbox_ready = true, .in_world = false, .in_fight = true};
    CHECK(rtc::gate(fight) == rtc::Off::InFight);
}

static void every_state_has_words_and_a_code()
{
    for (auto off : {rtc::Off::None, rtc::Off::Disabled, rtc::Off::NoSandbox, rtc::Off::NotSandbox, rtc::Off::NotInWorld,
                     rtc::Off::InFight})
    {
        CHECK(!rtc::why(off).empty());
        CHECK(!rtc::code(off).empty());
    }
    CHECK(rtc::code(rtc::Off::None) == "on");
    CHECK(rtc::code(rtc::Off::NotSandbox) == "not-sandbox");
}

static bool near(double a, double b, double within = 0.01) { return std::abs(a - b) <= within; }

static void turns_go_the_short_way()
{
    CHECK(near(rtc::turn(0, 90), 90));
    CHECK(near(rtc::turn(0, -90), -90));
    CHECK(near(rtc::turn(170, -170), 20));     // across 180, to the right
    CHECK(near(rtc::turn(-170, 170), -20));
    CHECK(near(rtc::turn(10, 370), 0));
    CHECK(near(rtc::turn(0, 180), 180));       // straight behind: 180, not -180
    CHECK(near(rtc::yaw_to({0, 0, 0}, {100, 0, 0}), 0));
    CHECK(near(rtc::yaw_to({0, 0, 0}, {0, 100, 0}), 90));   // +Y is to the right
    CHECK(near(rtc::pitch_to({0, 0, 0}, {100, 0, 100}), 45));
}

static void lock_picks_nearest_the_middle_of_the_view()
{
    const rtc::Vec me{0, 0, 0};
    // Looking along +X: one straight ahead 800 away, one 500 away but 40 degrees right, one behind, one too far.
    std::vector<rtc::Candidate> all{
        {1, {800, 0, 0}, true},
        {2, {383, 321, 0}, true},
        {3, {-300, 0, 0}, true},
        {4, {1600, 0, 0}, true},
    };
    CHECK(rtc::pick_target(me, 0, all) == 0);
    // Looking 40 degrees right: the near one.
    CHECK(rtc::pick_target(me, 40, all) == 1);
    // Unseen ones aren't picked.
    all[0].seen = false;
    CHECK(rtc::pick_target(me, 0, all) == 1);
    // Behind your cat and nothing in front: none.
    std::vector<rtc::Candidate> behind{{3, {-300, 0, 0}, true}};
    CHECK(rtc::pick_target(me, 0, behind) == -1);
    // Of two almost in line, the nearer.
    std::vector<rtc::Candidate> line{{1, {1200, 20, 0}, true}, {2, {400, 0, 0}, true}};
    CHECK(rtc::pick_target(me, 0, line) == 1);
    CHECK(rtc::pick_target(me, 0, {}) == -1);
}

static void next_target_goes_to_the_side_asked()
{
    const rtc::Vec me{0, 0, 0};
    std::vector<rtc::Candidate> all{
        {1, {800, 0, 0}, true},      // locked on, straight ahead
        {2, {600, 400, 0}, true},    // right, about 34 degrees
        {3, {300, 600, 0}, true},    // further right, about 63 degrees
        {4, {700, -300, 0}, true},   // left, about 23 degrees
    };
    CHECK(rtc::next_target(me, all[0].at, 1, +1, all) == 1);
    CHECK(rtc::next_target(me, all[0].at, 1, -1, all) == 3);
    // From the right one, further right is the next.
    CHECK(rtc::next_target(me, all[1].at, 2, +1, all) == 2);
    // Nothing further right of the last one.
    CHECK(rtc::next_target(me, all[2].at, 3, +1, all) == -1);
    // An unseen or too far one is skipped.
    all[1].seen = false;
    CHECK(rtc::next_target(me, all[0].at, 1, +1, all) == 2);
    all[2].at = {2000, 4000, 0};
    CHECK(rtc::next_target(me, all[0].at, 1, +1, all) == -1);
}

static void lock_lets_go()
{
    CHECK(rtc::keep_lock(false, 1000, 0) == rtc::Release::Keep);
    CHECK(rtc::keep_lock(true, 1000, 0) == rtc::Release::Gone);
    CHECK(rtc::keep_lock(false, 2300, 0) == rtc::Release::TooFar);
    CHECK(rtc::keep_lock(false, 1000, 2.5) == rtc::Release::Unseen);
    CHECK(rtc::keep_lock(false, 1000, 1.5) == rtc::Release::Keep);
    CHECK(rtc::code(rtc::Release::TooFar) == "too-far");
}

static void the_camera_eases_to_the_target()
{
    const rtc::Vec me{0, 0, 0}, ahead_right{0, 1000, 0};   // the target at yaw 90, level with your cat
    rtc::Rot r{0, 0};
    // One step: part of the way, the right way.
    rtc::Rot one = rtc::lock_rotation(r, me, ahead_right, 1.0 / 60);
    CHECK(one.yaw > 0 && one.yaw < 90);
    CHECK(one.pitch < 0 && one.pitch > -12);
    // Two seconds of frames: there.
    for (int i = 0; i < 120; ++i) r = rtc::lock_rotation(r, me, ahead_right, 1.0 / 60);
    CHECK(near(r.yaw, 90, 0.1));
    CHECK(near(r.pitch, -12, 0.1));
    // Unreal's 0-360 pitch: 350 is -10.
    rtc::Rot level = rtc::lock_rotation({350, 90}, me, ahead_right, 10);
    CHECK(near(level.pitch, -12, 0.1));
    // Across 180: the short way round.
    rtc::Rot back = rtc::lock_rotation({0, 170}, me, {-1000, -100, 0}, 1.0 / 60);
    CHECK(back.yaw > 170);
    // Far above: the pitch kept in bounds.
    rtc::Rot up = rtc::lock_rotation({0, 0}, me, {100, 0, 1000}, 10);
    CHECK(near(up.pitch, 15, 0.1));
}

int main()
{
    gate_is_on_only_for_a_sandbox_cat_in_the_world();
    gate_says_the_first_reason();
    every_state_has_words_and_a_code();
    turns_go_the_short_way();
    lock_picks_nearest_the_middle_of_the_view();
    next_target_goes_to_the_side_asked();
    lock_lets_go();
    the_camera_eases_to_the_target();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
