# Aggregator — Class Reference

**File:** `tests/aggregatorClass/Aggregator.h`
**Language:** C++11, header-only
**Dependencies:** `<limits>`, `<cstdint>` (standard library only — no ESP-IDF headers)

---

## Purpose

`Aggregator` accumulates a stream of `float` samples and tracks their **minimum**, **maximum**, and **running mean**. When `getSnapshot()` is called all three statistics and the sample count are returned in a `Snapshot` struct and **all accumulators are reset**, modelling the 1-second publish window used by the `sensorEmulatorTask` JSON serialiser.

Each JSON field that carries a `{min, max, avg}` sub-object (see `documentation/json.md`) maps directly to one `Aggregator` instance.

### Why a struct instead of three individual getters

Calling separate `getMin()`, `getMax()`, and `getAvg()` methods creates a **race window** between each call: an ISR or another RTOS task could invoke `add()` between calls, making min/max belong to a different sample population than avg. `getSnapshot()` copies all four fields and resets in a **single call**, so the caller only needs one critical-section guard around that one call:

```cpp
// FreeRTOS                               |  bare-metal / ISR
xSemaphoreTake(mutex, portMAX_DELAY);     |  __disable_irq();
auto s = agg.getSnapshot();              |  auto s = agg.getSnapshot();
xSemaphoreGive(mutex);                   |  __enable_irq();
```

The guard covers the minimum possible window — one function call — regardless of which concurrency primitive is used.

---

## Algorithm

The mean is maintained using **Welford's online algorithm**:

$$
\bar{x}_n = \bar{x}_{n-1} + \frac{x_n - \bar{x}_{n-1}}{n}
$$

Implemented as:

```cpp
++count_;
avg_ += (value - avg_) / static_cast<float>(count_);
```

| Property | Detail |
|----------|--------|
| Memory | O(1) — no sample buffer required |
| Passes | Single-pass (incremental) |
| Numerical stability | Stable — avoids large intermediate sums |
| Reset trigger | `getSnapshot()` or explicit `reset()` |

---

## API

### Constructor

```cpp
Aggregator()
```

Initialises all accumulators to their empty-window state:

| Member | Initial value |
|--------|---------------|
| `avg_` | `0.0f` |
| `min_` | `+∞` (`std::numeric_limits<float>::infinity()`) |
| `max_` | `−∞` (`-std::numeric_limits<float>::infinity()`) |
| `count_` | `0` |

---

### `struct Snapshot`

POD struct returned by `getSnapshot()`. All four fields belong to the same sample population — they are captured inside a single function call before the reset occurs.

```cpp
struct Snapshot {
    float min;   // +infinity if no samples in window
    float max;   // -infinity if no samples in window
    float avg;   //  0.0f    if no samples in window
    int   count; //  0       if no samples in window
};
```

---

### `Snapshot getSnapshot()`

Captures `min`, `max`, `avg`, and `count` into a `Snapshot`, then **resets all accumulators** to their initial values. Because the entire read-and-reset happens inside one function call, only this one call needs to be protected by a critical-section guard against concurrent `add()` invocations.

Returns a zero/infinity-initialised `Snapshot` if called on an empty window.

---

### `void add(float value)`

Submit a new sample into the current window.

- Increments `count_`
- Updates running mean with Welford's step
- Updates `min_` and `max_` if `value` is outside the current range

---

### `void reset()`

Manually resets all accumulators to their initial state without reading a snapshot. Use this to discard a window without publishing.

---

## Reset Semantics

| Call | Resets avg | Resets min/max | Resets count | Returns |
|------|-----------|----------------|--------------|------|
| `getSnapshot()` | yes | yes | yes | `Snapshot{min, max, avg, count}` — all from same window |
| `reset()` | yes | yes | yes | void |

---

## Typical Usage Pattern

```cpp
Aggregator thr;   // one instance per JSON field

// Samples arrive from CAN / UART during the 1-second window:
thr.add(45.0f);
thr.add(50.0f);
thr.add(47.0f);

// At the 1-second publish tick — one guarded call captures and resets:
// (guard with xSemaphoreTake/Give for FreeRTOS, or __disable_irq/__enable_irq for bare-metal)
auto s = thr.getSnapshot();   // s.min=45, s.max=50, s.avg=47.33 — resets for next window

// Build JSON:  "thr": {"min": 45, "max": 50, "avg": 47.33}
```

---

## Integration with sensorEmulatorTask

In `sensorEmulatorTask.cpp` each JSON field with a `{min, max, avg}` sub-object owns one `Aggregator` instance. The task loop:

1. Calls `add()` on each new sample as it arrives from CAN or UART.
2. On the 1-second `vTaskDelayUntil` tick, calls `getSnapshot()` inside a critical section (FreeRTOS mutex or `taskENTER_CRITICAL`) to atomically read all three statistics and reset the window in one call.
3. Serialises `s.min`, `s.max`, `s.avg` into the compact JSON string.
4. Wraps the string in the SOH/DLE-stuffed/CRC-16/CAN binary frame and transmits over UART1 TX (GPIO4).

---

## Unit Tests

**File:** `tests/aggregatorClass/main.cpp`
**Framework:** Catch2 v2.13.10
**Build:** Code::Blocks MinGW — `AggregatorTests.cbp`

| # | Test case | Assertions | What is verified |
|---|-----------|-----------|-----------------|
| 1 | Initial state | 6 | snapshot avg=0, min=+∞, max=−∞, count=0 |
| 2 | Single value | 7 | snapshot min==max==avg==value, count=1; negative value |
| 3 | Two values | 7 | snapshot min=2, max=8, avg=5, count=2; insertion order independence |
| 4 | Identical values | 3 | snapshot min==max==avg==5 |
| 5 | Mixed +/− | 3 | snapshot min=-3, max=3, avg=0 |
| 6 | Welford accuracy | 9 | sequences 1–10, 1–100, RPM range ±10 000 |
| 7 | `getSnapshot()` resets | 12 | snapshot correct; min/max reset to ±∞; second window independent |
| 8 | Manual `reset()` | 4 | discards data; subsequent window independent |
| 9 | 1 000 samples | 3 | tank pressure 250.3 Bar repeated 1 000× |
| 10 | Alternating ±A | 3 | 200 pairs of ±4095.5 → avg ≈ 0, min/max correct via snapshot |
| 11 | Two consecutive windows | 6 | simulates two 1-second publish cycles end-to-end |

**Result:** 63 assertions in 11 test cases — all passed (exit code 0).

Run from the repository root:

```bat
"C:\Program Files\CodeBlocks\MinGW\bin\g++.exe" -std=c++11 -Wall -fexceptions ^
    -o AggregatorTests.exe main.cpp
AggregatorTests.exe
```
