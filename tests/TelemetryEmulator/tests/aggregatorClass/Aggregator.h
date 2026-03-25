/*!
 * @file Aggregator.h
 * @brief Running min/max/average accumulator using Welford's online algorithm.
 *
 * Accepts a stream of float samples via add().  On each call to getSnapshot()
 * all three statistics plus the sample count are returned in a Snapshot struct
 * and the accumulator is reset, modelling the 1-second integration window used
 * by the sensorEmulatorTask JSON publisher.
 *
 * ### Usage pattern
 * @code
 *   Aggregator a;
 *   a.add(1.0f); a.add(3.0f); a.add(2.0f);
 *   Aggregator::Snapshot s = a.getSnapshot();  // s.min=1, s.max=3, s.avg=2 — resets
 * @endcode
 *
 * ### Why a struct instead of three individual getters
 * Calling getMin(), getMax(), and getAvg() separately creates a race window
 * between each call: an ISR or another task could invoke add() between them,
 * making min/max belong to a different sample population than avg.
 * getSnapshot() copies all four fields and resets in a single call, so the
 * caller needs only one critical-section guard:
 *
 * @code
 *   // FreeRTOS                        |  bare-metal / ISR
 *   xSemaphoreTake(mutex, portMAX_DELAY); |  __disable_irq();
 *   auto s = a.getSnapshot();          |  auto s = a.getSnapshot();
 *   xSemaphoreGive(mutex);             |  __enable_irq();
 * @endcode
 *
 * ### Average algorithm — Welford's online algorithm
 * @code
 *   avg_ += (value - avg_) / count_;
 * @endcode
 * Single-pass, O(1) memory, numerically stable.
 *
 * ### Reset semantics
 * getSnapshot() resets min_, max_, avg_, and count_ to their initial states.
 * getCount() is non-resetting and may be called at any time.
 */

#pragma once

#include <limits>
#include <cstdint>

class Aggregator
{
public:
    /*!
     * @brief Immutable snapshot of one integration window, returned by getSnapshot().
     *
     * All four fields belong to the same sample population — they are copied
     * inside a single function call before the reset occurs, so the caller
     * only needs to guard that one call against concurrent add() invocations.
     */
    struct Snapshot
    {
        float min;    ///< Minimum sample value; +infinity if no samples in window.
        float max;    ///< Maximum sample value; -infinity if no samples in window.
        float avg;    ///< Running mean (Welford); 0.0f if no samples in window.
        int   count;  ///< Number of samples in the window.
    };

    Aggregator() { reset(); }

    /*!
     * @brief Submit a new sample.
     * @param value  The sample value to incorporate.
     */
    void add(float value)
    {
        ++count_;
        avg_ += (value - avg_) / static_cast<float>(count_);
        if (value < min_) min_ = value;
        if (value > max_) max_ = value;
    }

    /*!
     * @brief Return a snapshot of the current window and reset the accumulator.
     *
     * Copies min, max, avg, and count into a Snapshot struct, then resets all
     * accumulators to their initial values.  Because all reads and the reset
     * happen inside a single function call, the caller only needs to guard
     * this one call against concurrent add() invocations.
     *
     * @return  Snapshot of the completed window.
     *          avg=0, count=0, min=+inf, max=-inf on an empty window.
     */
    Snapshot getSnapshot()
    {
        Snapshot s { min_, max_, avg_, count_ };
        reset();
        return s;
    }

    /*!
     * @brief Manually reset without reading a snapshot.
     * Use this to discard a window without publishing.
     */
    void reset()
    {
        avg_   = 0.0f;
        min_   = std::numeric_limits<float>::infinity();
        max_   = -std::numeric_limits<float>::infinity();
        count_ = 0;
    }

private:
    float avg_;
    float min_;
    float max_;
    int   count_;
};
