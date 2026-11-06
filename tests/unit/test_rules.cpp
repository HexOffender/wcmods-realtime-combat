// Unit tests for realtime-combat/cpp/src/rules.hpp: plain C++20, no game, no UE4SS. tests/unit/run.py builds and runs
// them (g++, clang++ or Visual Studio's cl), and so does GitHub's CI.
#include "rules.hpp"

#include <cstdio>
#include <string>

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

int main()
{
    gate_is_on_only_for_a_sandbox_cat_in_the_world();
    gate_says_the_first_reason();
    every_state_has_words_and_a_code();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
