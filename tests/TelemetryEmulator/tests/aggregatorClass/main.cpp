/*!
 * @file main.cpp
 * @brief Catch2 host-side unit tests for Aggregator.
 *
 * Tests cover:
 *   - Initial/empty state via getSnapshot()
 *   - Single value: snapshot.avg == snapshot.min == snapshot.max == value
 *   - Two values: correct mean, min, max, count in one snapshot
 *   - Multiple identical values: mean unchanged, min == max
 *   - Mixed positive/negative values
 *   - Welford accuracy for a known integer sequence
 *   - getSnapshot() resets all accumulators atomically
 *   - reset() mid-stream
 *   - Large sample count (1000 uniform values)
 *   - Alternating positive/negative cancellation
 *   - Two consecutive 1-second windows
 */

#define CATCH_CONFIG_MAIN
#include "../../../catch2/catch.hpp"
#include "Aggregator.h"

#include <cmath>
#include <limits>

/* ── helpers ---------------------------------------------------------------- */
static constexpr float EPS = 1e-4f;   // relative tolerance for Approx()

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 1. Initial / empty state                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: initial state", "[aggregator][init]")
{
    Aggregator a;

    SECTION("snapshot avg is 0 on empty window")
    {
        REQUIRE(a.getSnapshot().avg == Approx(0.0f));
    }
    SECTION("snapshot min is +infinity on empty window")
    {
        auto s = a.getSnapshot();
        REQUIRE(std::isinf(s.min));
        REQUIRE(s.min > 0.0f);
    }
    SECTION("snapshot max is -infinity on empty window")
    {
        auto s = a.getSnapshot();
        REQUIRE(std::isinf(s.max));
        REQUIRE(s.max < 0.0f);
    }
    SECTION("snapshot count is 0 on empty window")
    {
        REQUIRE(a.getSnapshot().count == 0);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 2. Single value                                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: single value", "[aggregator][single]")
{
    Aggregator a;
    a.add(42.0f);

    SECTION("snapshot: min == max == avg == value, count == 1")
    {
        auto s = a.getSnapshot();
        REQUIRE(s.min   == Approx(42.0f));
        REQUIRE(s.max   == Approx(42.0f));
        REQUIRE(s.avg   == Approx(42.0f));
        REQUIRE(s.count == 1);
    }
    SECTION("single negative value")
    {
        Aggregator b;
        b.add(-7.5f);
        auto s = b.getSnapshot();
        REQUIRE(s.min == Approx(-7.5f));
        REQUIRE(s.max == Approx(-7.5f));
        REQUIRE(s.avg == Approx(-7.5f));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 3. Two values                                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: two values", "[aggregator][two]")
{
    Aggregator a;
    a.add(2.0f);
    a.add(8.0f);

    SECTION("snapshot: min=2, max=8, avg=5, count=2")
    {
        auto s = a.getSnapshot();
        REQUIRE(s.min   == Approx(2.0f));
        REQUIRE(s.max   == Approx(8.0f));
        REQUIRE(s.avg   == Approx(5.0f));
        REQUIRE(s.count == 2);
    }
    SECTION("insertion order does not affect result")
    {
        Aggregator b;
        b.add(8.0f);
        b.add(2.0f);
        auto s = b.getSnapshot();
        REQUIRE(s.min == Approx(2.0f));
        REQUIRE(s.max == Approx(8.0f));
        REQUIRE(s.avg == Approx(5.0f));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 4. Identical values                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: identical values", "[aggregator][identical]")
{
    Aggregator a;
    for (int i = 0; i < 10; ++i)
        a.add(5.0f);

    SECTION("snapshot: min == max == avg == 5")
    {
        auto s = a.getSnapshot();
        REQUIRE(s.min == Approx(5.0f));
        REQUIRE(s.max == Approx(5.0f));
        REQUIRE(s.avg == Approx(5.0f));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 5. Mixed positive and negative values                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: mixed positive and negative", "[aggregator][mixed]")
{
    // Values: -3, -1, 0, 1, 3  →  sum = 0, avg = 0, min = -3, max = 3
    Aggregator a;
    a.add(-3.0f);
    a.add(-1.0f);
    a.add( 0.0f);
    a.add( 1.0f);
    a.add( 3.0f);

    SECTION("snapshot: min=-3, max=3, avg=0")
    {
        auto s = a.getSnapshot();
        REQUIRE(s.min == Approx(-3.0f));
        REQUIRE(s.max == Approx( 3.0f));
        REQUIRE(s.avg == Approx( 0.0f).margin(EPS));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 6. Welford accuracy — known integer sequence 1…N                            */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: Welford accuracy for 1..N sequences", "[aggregator][welford]")
{
    // Mean of 1..10 = 5.5,  min = 1,  max = 10
    SECTION("sequence 1 to 10")
    {
        Aggregator a;
        for (int i = 1; i <= 10; ++i)
            a.add(static_cast<float>(i));
        auto s = a.getSnapshot();
        REQUIRE(s.min == Approx(1.0f));
        REQUIRE(s.max == Approx(10.0f));
        REQUIRE(s.avg == Approx(5.5f).epsilon(EPS));
    }

    // Mean of 1..100 = 50.5
    SECTION("sequence 1 to 100")
    {
        Aggregator a;
        for (int i = 1; i <= 100; ++i)
            a.add(static_cast<float>(i));
        auto s = a.getSnapshot();
        REQUIRE(s.min == Approx(1.0f));
        REQUIRE(s.max == Approx(100.0f));
        REQUIRE(s.avg == Approx(50.5f).epsilon(EPS));
    }

    // Motor RPM range: -10000 … +10000, mean = 0
    SECTION("motor RPM range symmetric sequence")
    {
        Aggregator a;
        for (int i = -10000; i <= 10000; ++i)
            a.add(static_cast<float>(i));
        auto s = a.getSnapshot();
        REQUIRE(s.min == Approx(-10000.0f));
        REQUIRE(s.max == Approx( 10000.0f));
        REQUIRE(s.avg == Approx(0.0f).margin(0.01f));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 7. getSnapshot() resets all accumulators                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: getSnapshot() resets accumulators", "[aggregator][reset]")
{
    Aggregator a;
    a.add(10.0f);
    a.add(20.0f);
    auto s1 = a.getSnapshot();   // avg=15, resets

    SECTION("snapshot contains correct values for first window")
    {
        REQUIRE(s1.min   == Approx(10.0f));
        REQUIRE(s1.max   == Approx(20.0f));
        REQUIRE(s1.avg   == Approx(15.0f));
        REQUIRE(s1.count == 2);
    }
    SECTION("snapshot of empty window after reset has avg=0")
    {
        REQUIRE(a.getSnapshot().avg == Approx(0.0f));
    }
    SECTION("second window is independent of first")
    {
        a.add(3.0f);
        a.add(7.0f);
        auto s2 = a.getSnapshot();
        REQUIRE(s2.min == Approx(3.0f));
        REQUIRE(s2.max == Approx(7.0f));
        REQUIRE(s2.avg == Approx(5.0f));
    }
    SECTION("snapshot min is +infinity on empty window after reset")
    {
        auto s = a.getSnapshot();
        REQUIRE(std::isinf(s.min));
        REQUIRE(s.min > 0.0f);
    }
    SECTION("snapshot max is -infinity on empty window after reset")
    {
        auto s = a.getSnapshot();
        REQUIRE(std::isinf(s.max));
        REQUIRE(s.max < 0.0f);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 8. Manual reset() mid-stream                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: manual reset() discards accumulated data", "[aggregator][reset]")
{
    Aggregator a;
    a.add(100.0f);
    a.add(200.0f);
    a.reset();

    SECTION("snapshot avg is 0 after reset()")
    {
        REQUIRE(a.getSnapshot().avg == Approx(0.0f));
    }
    SECTION("data after reset() is independent")
    {
        a.add(4.0f);
        a.add(6.0f);
        auto s = a.getSnapshot();
        REQUIRE(s.min == Approx(4.0f));
        REQUIRE(s.max == Approx(6.0f));
        REQUIRE(s.avg == Approx(5.0f));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 9. Large sample count — 1000 uniform values at 250.3 (tank pressure typical) */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: 1000 identical samples (tank pressure)", "[aggregator][large]")
{
    Aggregator a;
    const float val = 250.3f;
    for (int i = 0; i < 1000; ++i)
        a.add(val);

    SECTION("snapshot: min == max == avg == 250.3")
    {
        auto s = a.getSnapshot();
        REQUIRE(s.min == Approx(val).epsilon(EPS));
        REQUIRE(s.max == Approx(val).epsilon(EPS));
        REQUIRE(s.avg == Approx(val).epsilon(EPS));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 10. Alternating +/- values cancel to ~0                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: alternating +A / -A values average to zero", "[aggregator][cancel]")
{
    Aggregator a;
    const float A = 4095.5f;   // accX amplitude
    const int   N = 200;
    for (int i = 0; i < N; ++i)
    {
        a.add( A);
        a.add(-A);
    }

    SECTION("snapshot: min=-A, max=+A, avg approximately zero")
    {
        auto s = a.getSnapshot();
        REQUIRE(s.min == Approx(-A).epsilon(EPS));
        REQUIRE(s.max == Approx( A).epsilon(EPS));
        REQUIRE(s.avg == Approx(0.0f).margin(0.1f));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* 11. Two consecutive windows — simulate 1 s publish cycle                    */
/* ═══════════════════════════════════════════════════════════════════════════ */
TEST_CASE("Aggregator: two consecutive 1-second windows", "[aggregator][window]")
{
    Aggregator a;

    // Window 1: throttle 10%, 50%, 90%  →  avg = 50, min = 10, max = 90
    a.add(10.0f);
    a.add(50.0f);
    a.add(90.0f);
    auto s1 = a.getSnapshot();   // resets

    // Window 2: throttle 20%, 40%  →  avg = 30, min = 20, max = 40
    a.add(20.0f);
    a.add(40.0f);
    auto s2 = a.getSnapshot();   // resets

    SECTION("window 1 average") { REQUIRE(s1.avg == Approx(50.0f).epsilon(EPS)); }
    SECTION("window 1 min")     { REQUIRE(s1.min == Approx(10.0f)); }
    SECTION("window 1 max")     { REQUIRE(s1.max == Approx(90.0f)); }
    SECTION("window 2 average") { REQUIRE(s2.avg == Approx(30.0f).epsilon(EPS)); }
    SECTION("window 2 min")     { REQUIRE(s2.min == Approx(20.0f)); }
    SECTION("window 2 max")     { REQUIRE(s2.max == Approx(40.0f)); }
}
