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

static void attacks_are_quick_and_land_inside()
{
    for (auto a : {rtc::Attack::Light, rtc::Attack::Heavy, rtc::Attack::Lunge})
    {
        const auto& s = rtc::spec(a);
        CHECK(s.from < s.hit && s.hit < s.to);
        CHECK(rtc::duration(s) > 0.5 && rtc::duration(s) < 1.5);
        CHECK(rtc::hit_at(s) > 0.2 && rtc::hit_at(s) < rtc::duration(s));
        CHECK(rtc::chain_from(s) >= rtc::hit_at(s) && rtc::chain_from(s) < rtc::duration(s));
        CHECK(!s.tag.empty() && !s.name.empty());
    }
    // the light one is the quickest and cheapest; the heavy one the strongest and dearest
    CHECK(rtc::duration(rtc::spec(rtc::Attack::Light)) < rtc::duration(rtc::spec(rtc::Attack::Heavy)));
    CHECK(rtc::spec(rtc::Attack::Light).stamina < rtc::spec(rtc::Attack::Heavy).stamina);
    CHECK(rtc::cat_damage(rtc::Attack::Heavy, 14) > rtc::cat_damage(rtc::Attack::Light, 14));
    CHECK(near(rtc::cat_damage(rtc::Attack::Light, 14), 23.4));
    CHECK(near(rtc::cat_damage(rtc::Attack::Light, 14, 2.0), 46.8));
}

static void stamina_runs_down_to_nothing()
{
    CHECK(rtc::can_spend(100, 12) && rtc::can_spend(6, 12));
    CHECK(!rtc::can_spend(5.9, 12) && !rtc::can_spend(0, 12));
    CHECK(rtc::stamina_pause > 0 && rtc::stamina_pause < 2);
    CHECK(near(rtc::spend(100, 12), 88));
    CHECK(near(rtc::spend(5, 12), 0));
}

static void reach_is_a_cone_in_front()
{
    const rtc::Vec me{0, 0, 0};
    CHECK(rtc::in_reach(me, 0, {150, 0, 0}, 190, 60));
    CHECK(rtc::in_reach(me, 0, {120, 100, 0}, 190, 60));     // 40 degrees right
    CHECK(!rtc::in_reach(me, 0, {50, 150, 0}, 190, 60));     // 72 degrees right
    CHECK(!rtc::in_reach(me, 0, {-150, 0, 0}, 190, 60));     // behind
    CHECK(!rtc::in_reach(me, 0, {250, 0, 0}, 190, 60));      // too far
    CHECK(!rtc::in_reach(me, 0, {100, 0, 300}, 190, 60));    // far above
    CHECK(rtc::in_reach(me, 0, {-20, 10, 0}, 190, 60));      // right on top of your cat
    CHECK(rtc::in_reach(me, 90, {0, 150, 0}, 190, 60));      // facing +Y
}

static void enemies_have_health_by_kind_and_level()
{
    CHECK(near(rtc::enemy_health(L"SK_Mice", 1), 30));
    CHECK(near(rtc::enemy_health(L"SK_Rabbit", 6), 45 * 1.6));
    CHECK(rtc::enemy_health(L"SK_Fox", 10) > rtc::enemy_health(L"SK_Rabbit", 10));
    CHECK(near(rtc::enemy_health(L"SK_Something", 1), 50));
    CHECK(near(rtc::enemy_health(L"SK_Mice", 1, 2.0), 60));
    CHECK(rtc::level_of_kind(L"Gameplay.Combat.Generator.Fixed.Rabbit.01.L6") == 6);
    CHECK(rtc::level_of_kind(L"Gameplay.Combat.Generator.Fixed.Fox.02.L12") == 12);
    CHECK(!rtc::level_of_kind(L"Gameplay.Combat.Generator.Fixed.Rabbit.01"));
    CHECK(!rtc::level_of_kind(L"x.L"));
    CHECK(!rtc::level_of_kind(L""));
    CHECK(near(rtc::after_hit(30, 23.4), 6.6));
    CHECK(near(rtc::after_hit(6.6, 23.4), 0));
    CHECK(near(rtc::after_hit(10, -5), 10));
}

static void the_dodge()
{
    const rtc::DodgeSpec d;
    CHECK(rtc::duration(d) > 0.4 && rtc::duration(d) < 1.0);
    CHECK(rtc::slide_start(d) < rtc::slide_end(d) && rtc::slide_end(d) <= rtc::duration(d));
    CHECK(near(rtc::slide_speed(d) * (rtc::slide_end(d) - rtc::slide_start(d)), d.distance));
    CHECK(rtc::safe_at(d, 0) && rtc::safe_at(d, 0.3) && !rtc::safe_at(d, 0.5) && !rtc::safe_at(d, -0.1));
    // the way the player's moving, as the camera sees it; standing still, straight back
    CHECK(near(rtc::dodge_yaw(0, 50, 1, 0), 0));
    CHECK(near(rtc::dodge_yaw(0, 50, 0, 1), 90));
    CHECK(near(rtc::dodge_yaw(30, 50, 0, -1), -60));
    CHECK(near(rtc::dodge_yaw(0, 50, 0, 0), 230));
    CHECK(near(rtc::dodge_yaw(0, 50, 0.1, 0.1), 230));
    // to the side: the hop that way, facing kept; forward or back: turned so the right hop goes that way
    rtc::DodgePose right = rtc::dodge_pose(0, 90);
    CHECK(right.right && near(right.cat_yaw, 0));
    rtc::DodgePose left = rtc::dodge_pose(0, -100);
    CHECK(!left.right && near(left.cat_yaw, 0));
    rtc::DodgePose back = rtc::dodge_pose(0, 180);
    CHECK(back.right && near(rtc::turn(back.cat_yaw + 90, 180), 0));
    rtc::DodgePose ahead = rtc::dodge_pose(20, 25);
    CHECK(ahead.right && near(rtc::turn(ahead.cat_yaw + 90, 25), 0));
    // out of an attack only once it's landed
    const auto& light = rtc::spec(rtc::Attack::Light);
    CHECK(!rtc::can_dodge_out_of(light, 0.1) && rtc::can_dodge_out_of(light, rtc::hit_at(light)));
}

// Rolls for step(), react() and take_hit(): the numbers given, in turn, then 0.99 (no chance comes up).
struct Rolls
{
    std::vector<double> next;
    size_t i = 0;
    double operator()() { return i < next.size() ? next[i++] : 0.99; }
};

static void foes_hit_back()
{
    const rtc::FoeSpec s;
    CHECK(rtc::hit_at(s) > 0.5 && rtc::hit_at(s) < rtc::duration(s));
    CHECK(rtc::commit_at(s) > 0 && rtc::commit_at(s) < rtc::hit_at(s));
    CHECK(near(rtc::enemy_damage(L"SK_Rabbit", 14), 23));
    CHECK(rtc::enemy_damage(L"SK_Fox", 5) > rtc::enemy_damage(L"SK_Mice", 5));
    CHECK(near(rtc::enemy_damage(L"SK_Rabbit", 1, 0.5), 5));
    CHECK(near(rtc::fresh_kill_for(L"SK_Rabbit"), 5) && near(rtc::fresh_kill_for(L"SK_Fox"), 0));

    // kinds fight differently: a badger guards most and takes most to break; a mouse hops and runs; a fox mixes
    const auto badger = rtc::profile(L"SK_Badger"), mouse = rtc::profile(L"SK_Mice"), fox = rtc::profile(L"SK_Fox");
    CHECK(badger.guard > fox.guard && fox.guard > mouse.guard && badger.posture > fox.posture);
    CHECK(mouse.dodge > fox.dodge && mouse.flee_below > 0 && badger.flee_below == 0);
    CHECK(fox.combo > 0 && fox.counter > 0 && fox.feint > 0);
    for (const auto* kind : {L"SK_Rabbit", L"SK_Rat", L"SK_Squirrel", L"SK_Bird", L"SK_Crow", L"SK_Snake", L"SK_Dog", L"SK_Thing"})
    {
        const auto k = rtc::profile(kind);
        CHECK(k.guard + k.dodge <= 1 && k.spacing > k.attack_range && k.cooldown_max >= k.cooldown_min && k.posture > 0);
    }
    rtc::FoeProfile plain;   // no chances: every roll comes out the same way
    plain.cooldown_min = plain.cooldown_max = 3;

    // it closes in to its spacing and paces, not into your cat's face; with its cooldown done and the turn free it
    // attacks, lands, stays open a moment, backs off and paces again, cooling down
    Rolls r;
    rtc::Foe f;
    f.cooldown = 1;
    CHECK(rtc::step(f, s, plain, 0.1, 900, 0, true, 1, r) == rtc::FoeDo::Nothing && f.state == rtc::FoeState::Approach);
    CHECK(rtc::step(f, s, plain, 0.1, 280, 0, true, 1, r) == rtc::FoeDo::Pace && f.state == rtc::FoeState::Pace);
    CHECK(rtc::step(f, s, plain, 0.5, 260, 0, true, 1, r) == rtc::FoeDo::Nothing);
    CHECK(rtc::step(f, s, plain, 0.5, 260, 0, true, 1, r) == rtc::FoeDo::StartAttack && f.state == rtc::FoeState::WindUp);
    CHECK(rtc::attacking(f) && near(f.strike_at, rtc::hit_at(s)) && near(f.hold, 0));
    CHECK(rtc::step(f, s, plain, 0.5, 150, 0, true, 1, r) == rtc::FoeDo::Nothing);
    CHECK(rtc::step(f, s, plain, 0.3, 150, 0, true, 1, r) == rtc::FoeDo::Land && f.state == rtc::FoeState::Recover);
    CHECK(rtc::step(f, s, plain, 0.3, 150, 0, true, 1, r) == rtc::FoeDo::Nothing);   // open: the time to hit it
    CHECK(rtc::step(f, s, plain, 0.5, 150, 0, true, 1, r) == rtc::FoeDo::Retreat && f.state == rtc::FoeState::Retreat);
    CHECK(near(f.cooldown, 3) && !rtc::attacking(f));
    CHECK(rtc::step(f, s, plain, 0.6, 250, 0, true, 1, r) == rtc::FoeDo::Pace && f.state == rtc::FoeState::Pace);
    CHECK(rtc::step(f, s, plain, 2.0, 250, 0, true, 1, r) == rtc::FoeDo::Nothing);   // cooling down
    // left too far behind while pacing: it closes in again
    CHECK(rtc::step(f, s, plain, 0.1, 600, 0, false, 1, r) == rtc::FoeDo::Approach && f.state == rtc::FoeState::Approach);
    // still far while closing in: it doesn't attack from there, whatever its cooldown
    rtc::Foe far_off;
    CHECK(rtc::step(far_off, s, plain, 0.1, 900, 0, true, 1, r) == rtc::FoeDo::Nothing && far_off.state == rtc::FoeState::Approach);
    // not its turn: it waits
    rtc::Foe g;
    CHECK(rtc::step(g, s, plain, 0.1, 250, 0, false, 1, r) != rtc::FoeDo::StartAttack);

    // pacing a while, it may turn round (a roll under 0.4) or not
    rtc::Foe pace;
    pace.state = rtc::FoeState::Pace;
    pace.cooldown = 99;
    Rolls turn{{0.1}};
    CHECK(rtc::step(pace, s, plain, s.pace_turn, 250, 0, true, 1, turn) == rtc::FoeDo::Pace && pace.side == -1);
    CHECK(rtc::step(pace, s, plain, s.pace_turn, 250, 0, true, 1, r) == rtc::FoeDo::Nothing && pace.side == -1);

    // a feint: the wind-up is held before the strike (Hold, then Release), and lands that much later
    rtc::FoeProfile feinter = plain;
    feinter.feint = 1;
    feinter.feint_min = feinter.feint_max = 0.6;
    rtc::Foe fe;
    fe.state = rtc::FoeState::Pace;
    Rolls fr{{0.0, 0.5}};
    CHECK(rtc::step(fe, s, feinter, 0.1, 250, 0, true, 1, fr) == rtc::FoeDo::StartAttack);
    CHECK(near(fe.hold, 0.6) && near(fe.strike_at, rtc::hit_at(s) + 0.6));
    CHECK(rtc::step(fe, s, feinter, rtc::commit_at(s), 150, 0, true, 1, r) == rtc::FoeDo::Hold);
    CHECK(rtc::step(fe, s, feinter, 0.3, 150, 0, true, 1, r) == rtc::FoeDo::Nothing);
    CHECK(rtc::step(fe, s, feinter, 0.35, 150, 0, true, 1, r) == rtc::FoeDo::Release);
    CHECK(rtc::step(fe, s, feinter, s.commit, 150, 0, true, 1, r) == rtc::FoeDo::Land);
    // a long frame that passes the whole hold: Hold, then Release on the next frame all the same
    rtc::Foe slow;
    rtc::wind_up(slow, s, false, 0.6);
    CHECK(rtc::step(slow, s, feinter, rtc::commit_at(s) + 0.7, 150, 0, true, 1, r) == rtc::FoeDo::Hold);
    CHECK(rtc::step(slow, s, feinter, 0.01, 150, 0, true, 1, r) == rtc::FoeDo::Release);

    // a combo: a second, quick strike straight after the first, and no third
    rtc::FoeProfile comboer = plain;
    comboer.combo = 1;
    rtc::Foe c;
    c.state = rtc::FoeState::Pace;
    Rolls cr{{0.5, 0.0}};   // no feint; the combo comes up
    CHECK(rtc::step(c, s, comboer, 0.1, 250, 0, true, 1, cr) == rtc::FoeDo::StartAttack);
    CHECK(rtc::step(c, s, comboer, rtc::hit_at(s), 150, 0, true, 1, cr) == rtc::FoeDo::Land);
    CHECK(rtc::step(c, s, comboer, s.combo_gap, 150, 0, true, 1, cr) == rtc::FoeDo::Combo && c.quick);
    CHECK(rtc::step(c, s, comboer, s.quick, 150, 0, true, 1, cr) == rtc::FoeDo::Land);
    Rolls again{{0.0}};
    CHECK(rtc::step(c, s, comboer, s.combo_gap, 150, 0, true, 1, again) == rtc::FoeDo::Nothing && c.state == rtc::FoeState::Recover);

    // your cat attacks it: pacing, it guards (a roll under its guard chance) or hops (under guard + dodge); mid-attack it
    // can't
    rtc::FoeProfile wary = plain;
    wary.guard = 0.3;
    wary.dodge = 0.3;
    rtc::Foe w;
    w.state = rtc::FoeState::Pace;
    CHECK(rtc::react(w, wary, 0.1) == rtc::FoeReaction::Guard && w.state == rtc::FoeState::Guard);
    w.t = 1.0;   // attacked again while guarding: it keeps its guard up as long again, whatever the roll
    CHECK(rtc::react(w, wary, 0.9) == rtc::FoeReaction::KeepGuard && w.state == rtc::FoeState::Guard && near(w.t, 0));
    w.state = rtc::FoeState::Pace;
    CHECK(rtc::react(w, wary, 0.5) == rtc::FoeReaction::Hop && w.state == rtc::FoeState::Hop);
    w.state = rtc::FoeState::Pace;
    CHECK(rtc::react(w, wary, 0.7) == rtc::FoeReaction::None && w.state == rtc::FoeState::Pace);
    rtc::wind_up(w, s, false, 0);
    CHECK(rtc::react(w, wary, 0.0) == rtc::FoeReaction::None && w.state == rtc::FoeState::WindUp);

    // a hop's first part is out of reach; after it, it's hit as usual, and paces again when it's done
    rtc::Foe h;
    h.state = rtc::FoeState::Hop;
    CHECK(rtc::take_hit(h, s, wary, true, false, 0.99).what == rtc::FoeHit::Missed);
    h.t = s.hop_safe + 0.01;
    CHECK(rtc::take_hit(h, s, wary, true, false, 0.99).what == rtc::FoeHit::Hit);
    CHECK(rtc::step(h, s, wary, s.hop, 250, 0, false, 1, r) == rtc::FoeDo::Pace);

    // guarding: a hit from in front does guard_chip and fills its posture (a heavy one twice); from behind it's a hit;
    // full, the guard breaks and it reels, open; with its counter chance it strikes back at once
    rtc::FoeProfile guarder = plain;
    guarder.posture = 60;
    guarder.counter = 0.5;
    rtc::Foe gd;
    gd.state = rtc::FoeState::Guard;
    gd.guard_for = 5;
    auto b = rtc::take_hit(gd, s, guarder, true, false, 0.99);
    CHECK(b.what == rtc::FoeHit::Blocked && near(b.part, s.guard_chip) && near(gd.posture, s.posture_hit));
    CHECK(rtc::take_hit(gd, s, guarder, false, false, 0.99).what == rtc::FoeHit::Hit && near(gd.posture, s.posture_hit));
    auto broke = rtc::take_hit(gd, s, guarder, true, true, 0.99);   // 25 + 50 >= 60
    CHECK(broke.what == rtc::FoeHit::Broken && near(broke.part, 0) && gd.state == rtc::FoeState::Reel);
    CHECK(near(gd.reel_for, s.broken_for) && near(gd.posture, 0));
    rtc::Foe ct;
    ct.state = rtc::FoeState::Guard;
    ct.guard_for = 5;
    CHECK(rtc::take_hit(ct, s, guarder, true, false, 0.2).what == rtc::FoeHit::Countered && ct.state == rtc::FoeState::WindUp);
    CHECK(ct.quick && near(ct.strike_at, s.quick));
    // posture comes down again while it isn't guarding; a guard ends after its time
    rtc::Foe pr;
    pr.state = rtc::FoeState::Pace;
    pr.cooldown = 99;
    pr.posture = 30;
    rtc::step(pr, s, guarder, 1.0, 250, 0, true, 1, r);
    CHECK(near(pr.posture, 30 - s.posture_regen));
    rtc::Foe ge;
    ge.state = rtc::FoeState::Guard;
    ge.guard_for = 1.2;
    CHECK(rtc::step(ge, s, guarder, 1.3, 250, 0, true, 1, r) == rtc::FoeDo::Pace);

    // hit in its wind-up: it reels, then backs off; after it's landed a hit doesn't stop it
    rtc::Foe st;
    rtc::wind_up(st, s, false, 0);
    CHECK(rtc::take_hit(st, s, plain, true, false, 0.99).what == rtc::FoeHit::Staggered && st.state == rtc::FoeState::Reel);
    CHECK(rtc::step(st, s, plain, s.stagger + 0.01, 150, 0, true, 1, r) == rtc::FoeDo::Retreat);
    rtc::Foe k;
    rtc::wind_up(k, s, false, 0);
    rtc::step(k, s, plain, rtc::hit_at(s), 150, 0, true, 1, r);   // landed
    CHECK(rtc::take_hit(k, s, plain, true, false, 0.99).what == rtc::FoeHit::Hit && k.state == rtc::FoeState::Recover);
    // parried, whatever it was doing: it reels as long as asked
    rtc::parried(k, 2.0);
    CHECK(k.state == rtc::FoeState::Reel);
    CHECK(rtc::step(k, s, plain, 1.5, 150, 0, true, 1, r) == rtc::FoeDo::Nothing);
    CHECK(rtc::step(k, s, plain, 0.6, 150, 0, true, 1, r) == rtc::FoeDo::Retreat);

    // prey that's losing runs for it (not mid-strike), and once far enough it's got away
    rtc::Foe run;
    run.state = rtc::FoeState::Pace;
    run.cooldown = 99;
    CHECK(rtc::step(run, s, mouse, 0.1, 250, 0, true, 0.5, r) == rtc::FoeDo::Nothing);
    CHECK(rtc::step(run, s, mouse, 0.1, 250, 0, true, 0.2, r) == rtc::FoeDo::Flee && run.state == rtc::FoeState::Flee);
    CHECK(rtc::step(run, s, mouse, 0.6, s.flee_escape + 100, 0, true, 0.2, r) == rtc::FoeDo::Nothing);
    CHECK(rtc::step(run, s, mouse, 0.6, s.flee_escape + 100, 0, true, 0.2, r) == rtc::FoeDo::GiveUp);
    rtc::Foe bite;
    rtc::wind_up(bite, s, false, 0);
    CHECK(rtc::step(bite, s, mouse, 0.1, 150, 0, true, 0.2, r) != rtc::FoeDo::Flee);
    rtc::Foe stout;   // not prey: it fights on
    CHECK(rtc::step(stout, s, badger, 0.1, 250, 0, false, 0.05, r) != rtc::FoeDo::Flee);

    // too far for too long: it gives up; led too far from where it joined, too
    rtc::Foe far;
    CHECK(rtc::step(far, s, plain, 2, 3000, 0, false, 1, r) == rtc::FoeDo::Nothing);
    CHECK(rtc::step(far, s, plain, 2, 3000, 0, false, 1, r) == rtc::FoeDo::GiveUp);
    rtc::Foe led;
    CHECK(rtc::step(led, s, plain, 2, 250, 2500, false, 1, r) != rtc::FoeDo::GiveUp);
    CHECK(rtc::step(led, s, plain, 2, 250, 2500, false, 1, r) == rtc::FoeDo::GiveUp);
    rtc::Foe back;
    rtc::step(back, s, plain, 2, 3000, 0, false, 1, r);
    rtc::step(back, s, plain, 0.1, 500, 0, false, 1, r);   // came back near: the count starts again
    CHECK(rtc::step(back, s, plain, 2, 3000, 0, false, 1, r) != rtc::FoeDo::GiveUp);
}

// Your cat walked as the game walks it: along the camera's right (side +1) or left, at `speed`, for `seconds`, the
// camera set by circle_yaw each frame. How far round it went (degrees, + anticlockwise seen from above with Unreal's
// axes) and its distance from the target at the end and at its farthest.
struct Lap
{
    double turned, r_end, r_max, r_min;
};
static Lap walk_round(rtc::Vec start, rtc::Vec target, int side, double speed, double seconds, double r0)
{
    rtc::Vec me = start;
    double turned = 0, last = rtc::yaw_to(target, me), r_max = 0, r_min = 1e9;
    const double dt = 1.0 / 60;
    for (double t = 0; t < seconds; t += dt)
    {
        const double cam = rtc::circle_yaw(me, target, r0, side);
        const double way = (cam + 90 * side) / rtc::degrees_per_radian;
        me.x += std::cos(way) * speed * dt;
        me.y += std::sin(way) * speed * dt;
        const double now = rtc::yaw_to(target, me);
        turned += rtc::turn(last, now);
        last = now;
        const double r = rtc::flat_distance(me, target);
        r_max = std::max(r_max, r);
        r_min = std::min(r_min, r);
    }
    return {turned, rtc::flat_distance(me, target), r_max, r_min};
}

static void circling_keeps_to_the_circle()
{
    const rtc::Vec target{0, 0, 0};
    // right, 3 m out, walking 1.75 m/s for a full lap's time: round once, the circle kept within 3%
    const double r0 = 300, lap = 2 * 3.14159265 * r0 / 175;
    Lap right = walk_round({0, -r0, 0}, target, +1, 175, lap, r0);
    CHECK(std::abs(std::abs(right.turned) - 360) < 15);
    CHECK(right.r_max < r0 * 1.03 && right.r_min > r0 * 0.97);
    // left goes round the other way
    Lap left = walk_round({0, -r0, 0}, target, -1, 175, lap, r0);
    CHECK(left.turned * right.turned < 0);
    CHECK(left.r_max < r0 * 1.03 && left.r_min > r0 * 0.97);
    // started off the circle (1.5 times out): it's drawn back to it
    Lap back = walk_round({0, -r0 * 1.5, 0}, target, +1, 175, 6, r0);
    CHECK(std::abs(back.r_end - r0) < r0 * 0.1);
    // running (4 m/s) round a close target keeps to it too
    Lap run = walk_round({0, -150, 0}, target, +1, 400, 3, 150);
    CHECK(run.r_max < 150 * 1.15 && run.r_min > 150 * 0.85);
    // on the circle the camera's on the line to the target
    CHECK(near(rtc::circle_yaw({0, -r0, 0}, target, r0, 1), 90));
    // only enough left or right counts
    CHECK(rtc::circling(0.8) == 1 && rtc::circling(-0.5) == -1 && rtc::circling(0.1) == 0);

}

static void the_flask()
{
    const rtc::FlaskSpec f;
    CHECK(rtc::duration(f) > 0.8 && rtc::duration(f) < 1.6);
    CHECK(f.heal_from < f.heal_to && f.heal_to <= rtc::duration(f));
    // all of it over the healing time, none before or after, a part for part of it
    CHECK(near(rtc::healed_between(f, 178, 0, rtc::duration(f)), 178 * 0.4));
    CHECK(near(rtc::healed_between(f, 178, 0, f.heal_from), 0));
    CHECK(near(rtc::healed_between(f, 178, f.heal_to, 5), 0));
    const double mid = (f.heal_from + f.heal_to) / 2;
    CHECK(near(rtc::healed_between(f, 178, f.heal_from, mid), 178 * 0.2));
    // frame by frame adds up to the whole
    double sum = 0;
    for (double t = 0; t < rtc::duration(f); t += 1.0 / 60) sum += rtc::healed_between(f, 100, t, t + 1.0 / 60);
    CHECK(near(sum, 40, 0.01));
    // travelling: a jump no running makes
    CHECK(rtc::travelled({0, 0, 0}, {4000, 0, 0}) && !rtc::travelled({0, 0, 0}, {50, 0, 0}));
}

static void guarding()
{
    const rtc::GuardSpec g;
    // not guarding, or hit from behind: the whole of it
    auto r = rtc::guard(false, 1, true, 100, 20);
    CHECK(r.what == rtc::Guarded::Hit && near(r.health, 20) && near(r.stamina, 0));
    r = rtc::guard(true, 1, false, 100, 20);
    CHECK(r.what == rtc::Guarded::Hit && near(r.health, 20));
    // guarding: a quarter, the rest off stamina
    r = rtc::guard(true, 1, true, 100, 20);
    CHECK(r.what == rtc::Guarded::Blocked && near(r.health, 5) && near(r.stamina, 20));
    // too little stamina: the guard breaks, the chip taken, the stamina gone
    r = rtc::guard(true, 1, true, 12, 20);
    CHECK(r.what == rtc::Guarded::GuardBroken && near(r.health, 5) && near(r.stamina, 12));
    // begun just before it lands: a parry, nothing taken
    r = rtc::guard(true, 0.1, true, 0, 20);
    CHECK(r.what == rtc::Guarded::Parried && near(r.health, 0) && near(r.stamina, 0));
    CHECK(rtc::guard(true, g.parry_window + 0.01, true, 100, 20).what == rtc::Guarded::Blocked);
    CHECK(rtc::parry_duration(g) > 0.3 && rtc::parry_duration(g) < 1);
    CHECK(rtc::code(rtc::Guarded::GuardBroken) == "guard-broken");
}

int main()
{
    guarding();
    the_flask();
    circling_keeps_to_the_circle();
    foes_hit_back();
    the_dodge();
    gate_is_on_only_for_a_sandbox_cat_in_the_world();
    gate_says_the_first_reason();
    every_state_has_words_and_a_code();
    turns_go_the_short_way();
    lock_picks_nearest_the_middle_of_the_view();
    next_target_goes_to_the_side_asked();
    lock_lets_go();
    the_camera_eases_to_the_target();
    attacks_are_quick_and_land_inside();
    stamina_runs_down_to_nothing();
    reach_is_a_cone_in_front();
    enemies_have_health_by_kind_and_level();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
