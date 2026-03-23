#pragma once
/*
 * waveforms_standalone.h
 *
 * Host-portable copy of the waveform generators from sensorEmulatorTask.cpp.
 * No ESP-IDF dependencies; only <cmath> is required.
 *
 * Keep in sync with main/sensorEmulatorTask.cpp.
 */

#include <cmath>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

static inline float wave_sine(float t, float period, float lo, float hi)
{
    const float mid = (hi + lo) * 0.5f;
    const float amp = (hi - lo) * 0.5f;
    return mid + amp * std::sin(2.0f * static_cast<float>(M_PI) * t / period);
}

static inline float wave_cosine(float t, float period, float lo, float hi)
{
    const float mid = (hi + lo) * 0.5f;
    const float amp = (hi - lo) * 0.5f;
    return mid + amp * std::cos(2.0f * static_cast<float>(M_PI) * t / period);
}

static inline float wave_triangle(float t, float period, float lo, float hi)
{
    const float mid = (hi + lo) * 0.5f;
    const float amp = (hi - lo) * 0.5f;
    const float phi = std::fmod(t, period) / period;
    const float raw = (phi < 0.5f) ? (4.0f * phi - 1.0f)
                                   : (3.0f - 4.0f * phi);
    return mid + amp * raw;
}

static inline float wave_square(float t, float period, float lo, float hi)
{
    const float phi = std::fmod(t, period) / period;
    return (phi < 0.5f) ? hi : lo;
}

static inline float wave_trapezoid(float t, float period, float lo, float hi)
{
    const float phi = std::fmod(t, period) / period;
    float val;
    if (phi < 0.25f) {
        val = phi / 0.25f;
    } else if (phi < 0.50f) {
        val = 1.0f;
    } else if (phi < 0.75f) {
        val = 1.0f - (phi - 0.50f) / 0.25f;
    } else {
        val = 0.0f;
    }
    return lo + val * (hi - lo);
}

static inline float wave_abs_sine(float t, float period, float lo, float hi)
{
    return lo + std::fabs(std::sin(2.0f * static_cast<float>(M_PI) * t / period)) * (hi - lo);
}
