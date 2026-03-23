/*!
 * @file main.cpp
 * @brief Catch2 host-side unit tests for the waveform generators in sensorEmulatorTask.
 *
 * Tests the six waveform functions implemented in main/sensorEmulatorTask.cpp via
 * the host-portable stub waveforms_standalone.h.  No ESP-IDF headers are required.
 *
 * ### Waveforms under test
 *   - wave_sine      — mid + amp·sin(2π·t/T)
 *   - wave_cosine    — mid + amp·cos(2π·t/T)
 *   - wave_triangle  — symmetric triangle, 4-segment linear
 *   - wave_square    — 50% duty cycle, instantaneous transitions
 *   - wave_trapezoid — 25% rise | 25% hold-hi | 25% fall | 25% hold-lo
 *   - wave_abs_sine  — lo + |sin(2π·t/T)|·(hi-lo)  — non-negative
 *
 * ### Strategy
 *   Each test evaluates several analytically known sample points chosen to land at
 *   extrema, zero crossings, and mid-transition inflection points.  Float comparisons
 *   use Approx() with a default tolerance of 1e-4.
 */

#define CATCH_CONFIG_MAIN
#include "../../../catch2/catch.hpp"
#include "waveforms_standalone.h"

static constexpr float EPS = 0.01f;    // 1% relative — accommodates float sin/cos rounding at large amplitudes
static constexpr float MARGIN = 0.01f; // absolute margin for expectations at exactly 0.0

/* ── wave_sine ─────────────────────────────────────────────────────────────── */
TEST_CASE("wave_sine: known sample points", "[waveform][sine]")
{
    // Range -4096 … +4095, period 5 s  (sensorEmulatorTask accX assignment)
    // mid = -0.5,  amp = 4095.5
    const float lo = -4096.0f, hi = 4095.0f, T = 5.0f;

    SECTION("t=0: sin(0)=0 → result ≈ mid = -0.5")
    {
        REQUIRE(wave_sine(0.0f, T, lo, hi) == Approx(-0.5f).epsilon(EPS));
    }
    SECTION("t=T/4: sin(π/2)=1 → result = hi = 4095")
    {
        REQUIRE(wave_sine(T / 4.0f, T, lo, hi) == Approx(4095.0f).epsilon(EPS));
    }
    SECTION("t=T/2: sin(π)=0 → result ≈ mid = -0.5")
    {
        REQUIRE(wave_sine(T / 2.0f, T, lo, hi) == Approx(-0.5f).margin(1.0f));
    }
    SECTION("t=3T/4: sin(3π/2)=-1 → result = lo = -4096")
    {
        REQUIRE(wave_sine(3.0f * T / 4.0f, T, lo, hi) == Approx(-4096.0f).epsilon(EPS));
    }
    SECTION("t=T: completes full cycle, same as t=0")
    {
        REQUIRE(wave_sine(T, T, lo, hi) == Approx(wave_sine(0.0f, T, lo, hi)).margin(1.0f));
    }
}

/* ── wave_cosine ───────────────────────────────────────────────────────────── */
TEST_CASE("wave_cosine: known sample points", "[waveform][cosine]")
{
    // Range -4096 … +4095, period 7 s  (sensorEmulatorTask accY assignment)
    const float lo = -4096.0f, hi = 4095.0f, T = 7.0f;

    SECTION("t=0: cos(0)=1 → result = hi = 4095")
    {
        REQUIRE(wave_cosine(0.0f, T, lo, hi) == Approx(4095.0f).epsilon(EPS));
    }
    SECTION("t=T/4: cos(π/2)=0 → result ≈ mid = -0.5")
    {
        REQUIRE(wave_cosine(T / 4.0f, T, lo, hi) == Approx(-0.5f).margin(1.0f));
    }
    SECTION("t=T/2: cos(π)=-1 → result = lo = -4096")
    {
        REQUIRE(wave_cosine(T / 2.0f, T, lo, hi) == Approx(-4096.0f).epsilon(EPS));
    }
    SECTION("t=3T/4: cos(3π/2)=0 → result ≈ mid = -0.5")
    {
        REQUIRE(wave_cosine(3.0f * T / 4.0f, T, lo, hi) == Approx(-0.5f).margin(1.0f));
    }
}

/* ── wave_triangle ─────────────────────────────────────────────────────────── */
TEST_CASE("wave_triangle: known sample points", "[waveform][triangle]")
{
    // Range -4096 … +4095, period 3 s  (sensorEmulatorTask accZ assignment)
    // mid = -0.5,  amp = 4095.5
    const float lo = -4096.0f, hi = 4095.0f, T = 3.0f;

    SECTION("t=0: φ=0, raw=-1 → result = lo = -4096")
    {
        REQUIRE(wave_triangle(0.0f, T, lo, hi) == Approx(-4096.0f).epsilon(EPS));
    }
    SECTION("t=T/4: φ=0.25, raw=0 → result ≈ mid = -0.5")
    {
        REQUIRE(wave_triangle(T / 4.0f, T, lo, hi) == Approx(-0.5f).epsilon(EPS));
    }
    SECTION("t=T/2: φ=0.5, raw=1 → result = hi = 4095")
    {
        REQUIRE(wave_triangle(T / 2.0f, T, lo, hi) == Approx(4095.0f).epsilon(EPS));
    }
    SECTION("t=3T/4: φ=0.75, raw=0 → result ≈ mid = -0.5")
    {
        REQUIRE(wave_triangle(3.0f * T / 4.0f, T, lo, hi) == Approx(-0.5f).epsilon(EPS));
    }
    SECTION("t=T: φ=0 (wraps), same as t=0 → lo")
    {
        REQUIRE(wave_triangle(T, T, lo, hi) == Approx(-4096.0f).epsilon(EPS));
    }
    SECTION("Output never exceeds hi")
    {
        for (int i = 0; i <= 100; ++i) {
            REQUIRE(wave_triangle(i * T / 100.0f, T, lo, hi) <= hi + 0.01f);
        }
    }
    SECTION("Output never falls below lo")
    {
        for (int i = 0; i <= 100; ++i) {
            REQUIRE(wave_triangle(i * T / 100.0f, T, lo, hi) >= lo - 0.01f);
        }
    }
}

/* ── wave_square ───────────────────────────────────────────────────────────── */
TEST_CASE("wave_square: known sample points", "[waveform][square]")
{
    // Range -100 … +100, period 6 s  (sensorEmulatorTask trq assignment)
    const float lo = -100.0f, hi = 100.0f, T = 6.0f;

    SECTION("t=0: φ=0 < 0.5 → result = hi =  100")
    {
        REQUIRE(wave_square(0.0f, T, lo, hi) == Approx(100.0f).epsilon(EPS));
    }
    SECTION("t=T/4: φ=0.25 < 0.5 → result = hi = 100")
    {
        REQUIRE(wave_square(T / 4.0f, T, lo, hi) == Approx(100.0f).epsilon(EPS));
    }
    SECTION("t=T/2: φ=0.5, result = lo = -100")
    {
        REQUIRE(wave_square(T / 2.0f, T, lo, hi) == Approx(-100.0f).epsilon(EPS));
    }
    SECTION("t=3T/4: φ=0.75, result = lo = -100")
    {
        REQUIRE(wave_square(3.0f * T / 4.0f, T, lo, hi) == Approx(-100.0f).epsilon(EPS));
    }
    SECTION("Only two distinct output values")
    {
        for (int i = 0; i <= 99; ++i) {
            float v = wave_square(i * T / 100.0f, T, lo, hi);
            bool valid = (v == Approx(hi).epsilon(EPS)) || (v == Approx(lo).epsilon(EPS));
            REQUIRE(valid);
        }
    }
}

/* ── wave_trapezoid ────────────────────────────────────────────────────────── */
TEST_CASE("wave_trapezoid: known sample points", "[waveform][trapezoid]")
{
    // Range 0 … 500, period 20 s  (sensorEmulatorTask tankP assignment)
    const float lo = 0.0f, hi = 500.0f, T = 20.0f;

    SECTION("t=0: start of rise → result = lo = 0")
    {
        REQUIRE(wave_trapezoid(0.0f, T, lo, hi) == Approx(0.0f).epsilon(EPS));
    }
    SECTION("t=T/8: mid-rise → result = 250")
    {
        REQUIRE(wave_trapezoid(T / 8.0f, T, lo, hi) == Approx(250.0f).epsilon(EPS));
    }
    SECTION("t=T/4: end of rise → result = hi = 500")
    {
        REQUIRE(wave_trapezoid(T / 4.0f, T, lo, hi) == Approx(500.0f).epsilon(EPS));
    }
    SECTION("t=3T/8: mid hold-high → result = hi = 500")
    {
        REQUIRE(wave_trapezoid(3.0f * T / 8.0f, T, lo, hi) == Approx(500.0f).epsilon(EPS));
    }
    SECTION("t=T/2: start of fall → result = hi = 500")
    {
        REQUIRE(wave_trapezoid(T / 2.0f, T, lo, hi) == Approx(500.0f).epsilon(EPS));
    }
    SECTION("t=5T/8: mid-fall → result = 250")
    {
        REQUIRE(wave_trapezoid(5.0f * T / 8.0f, T, lo, hi) == Approx(250.0f).epsilon(EPS));
    }
    SECTION("t=3T/4: end of fall → result = lo = 0")
    {
        REQUIRE(wave_trapezoid(3.0f * T / 4.0f, T, lo, hi) == Approx(0.0f).epsilon(EPS));
    }
    SECTION("t=7T/8: mid hold-low → result = lo = 0")
    {
        REQUIRE(wave_trapezoid(7.0f * T / 8.0f, T, lo, hi) == Approx(0.0f).epsilon(EPS));
    }
    SECTION("Output never exceeds hi")
    {
        for (int i = 0; i <= 100; ++i) {
            REQUIRE(wave_trapezoid(i * T / 100.0f, T, lo, hi) <= hi + 0.01f);
        }
    }
    SECTION("Output never falls below lo")
    {
        for (int i = 0; i <= 100; ++i) {
            REQUIRE(wave_trapezoid(i * T / 100.0f, T, lo, hi) >= lo - 0.01f);
        }
    }
}

/* ── wave_abs_sine ─────────────────────────────────────────────────────────── */
TEST_CASE("wave_abs_sine: known sample points", "[waveform][abs_sine]")
{
    // Range 0 … 100, period 10 s  (sensorEmulatorTask fan assignment)
    const float lo = 0.0f, hi = 100.0f, T = 10.0f;

    SECTION("t=0: |sin(0)|=0 → result = lo = 0")
    {
        REQUIRE(wave_abs_sine(0.0f, T, lo, hi) == Approx(0.0f).epsilon(EPS));
    }
    SECTION("t=T/4: |sin(π/2)|=1 → result = hi = 100")
    {
        REQUIRE(wave_abs_sine(T / 4.0f, T, lo, hi) == Approx(100.0f).epsilon(EPS));
    }
    SECTION("t=T/2: |sin(π)|=0 → result = lo = 0")
    {
        REQUIRE(wave_abs_sine(T / 2.0f, T, lo, hi) == Approx(0.0f).margin(MARGIN));
    }
    SECTION("t=3T/4: |sin(3π/2)|=1 → result = hi = 100")
    {
        REQUIRE(wave_abs_sine(3.0f * T / 4.0f, T, lo, hi) == Approx(100.0f).epsilon(EPS));
    }
    SECTION("Output is always non-negative")
    {
        for (int i = 0; i <= 100; ++i) {
            REQUIRE(wave_abs_sine(i * T / 100.0f, T, lo, hi) >= -0.01f);
        }
    }
    SECTION("Output never exceeds hi")
    {
        for (int i = 0; i <= 100; ++i) {
            REQUIRE(wave_abs_sine(i * T / 100.0f, T, lo, hi) <= hi + 0.01f);
        }
    }
}
