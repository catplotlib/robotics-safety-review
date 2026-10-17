# Test results — robotics-safety-review

Two inputs:
- `buggy_axis.cpp`: 9 planted bugs. It compiles cleanly with `-Wall -Wextra`; the only compiler warning is an unused parameter.
- `clean_limits.cpp`: intended to be correct, used to check for false positives.

Caveat: the same session wrote the bugs and the review, so this is a format and coverage check, not a blind test.

---

## Review 1: buggy_axis.cpp

**Mode:** Strict safety (1 kHz RT update loop sending CIA402 position targets to a drive)
**Scope reviewed:** buggy_axis.cpp › AxisController::setTarget, AxisController::update

## Summary
A cyclic position controller for one CIA402 axis. It has several defects that can cause unintended motion: automatic re-enable after a fault, no jump protection on enable, and a velocity limit that is detected but never enforced, with the wrong units. It should not run on hardware until the Critical issues are fixed.

## Safety Impact
This code is the last step before actuator commands reach the drive. The worst credible outcome is an uncommanded jump or fast motion right after a fault clears or at enable, with no operator action involved.

## Critical Issues
- **[buggy_axis.cpp › update, fault block]** After a fault, `enabled_` is set to false, and in the same cycle the `!enabled_` block overwrites the controlword with `0x000F`. The fault reset (`0x0080`) never reaches the drive, and the code tries to re-enable every cycle with no operator action. **Why it matters:** the axis can re-enable on its own as soon as the fault clears, which is unintended motion after a fault. **Fix:** latch a FAULT state; send a fault-reset rising edge (bit 7: 0 → 1) only on an explicit reset request, wait for the fault bit to clear, then re-run the enable sequence on a deliberate command. *Confidence: High*
- **[buggy_axis.cpp › update, enable block]** `0x000F` (Enable Operation) is sent from whatever state the drive is in, skipping Shutdown (`0x0006`) and Switch On (`0x0007`). `enabled_` is then set to true without reading back the statusword. **Why it matters:** the drive ignores or rejects the transition, so the software state and the drive state diverge. Later logic assumes an enabled axis that isn't enabled, or the reverse. **Fix:** step through the CIA402 transitions and advance only when the statusword (masked) confirms each state. *Confidence: High*
- **[buggy_axis.cpp › update]** On enable, `out.target_pos_counts` is not set to `in.actual_pos_counts`. **Why it matters:** in CSP the drive moves toward whatever target is in the PDO at enable, which may be stale or zero, producing a jump. **Fix:** set the target to the actual position before and during the enable transition; only start following `target_rad_` once Operation Enabled is confirmed. *Confidence: High*
- **[buggy_axis.cpp › update, velocity check]** When `vel > max_vel_`, the code logs it and then sends the target anyway. There's also no NaN/Inf check or position clamp on `target_rad_`, and the `int32_t` cast is undefined behavior for NaN or values past about ±25,700 rad. **Why it matters:** an out-of-range or garbage setpoint goes straight to the drive. **Fix:** check `std::isfinite`, rate-limit the step to `max_vel * dt`, clamp to joint limits, then convert to counts. On failure, enter a controlled stop. *Confidence: High*
- **[buggy_axis.cpp › max_vel_]** `max_vel_` is in deg/s (per the comment) but compared to `vel` in rad/s, so the limit is about 57× too loose. **Why it matters:** even once enforced, the limit would allow roughly 5,000 deg/s. **Fix:** convert at load time and name it `max_vel_rad_s_`. *Confidence: High*

## Major Issues
- **[buggy_axis.cpp › update]** `working_counter` is never checked. **Why it matters:** lost or partial EtherCAT frames go undetected and the loop keeps running on stale statusword and position data. **Fix:** compare against the expected WKC every cycle; on mismatch for N consecutive cycles, enter a fault state. *Confidence: High*
- **[buggy_axis.cpp › setTarget / update]** `target_rad_` is a plain `double` written by a non-RT thread and read by the RT thread. That's a data race (undefined behavior in C++) and can tear on some platforms. **Fix:** use `std::atomic<double>` or a lock-free single-producer, single-consumer buffer that also carries a timestamp for staleness checks. *Confidence: High*
- **[buggy_axis.cpp › update]** `in.statusword == 0x0237` compares the whole word. Warning, target-reached, and manufacturer bits change it while the drive stays in Operation Enabled. **Why it matters:** targets stop being sent mid-motion, which can trip a following-error fault. **Fix:** `(sw & 0x006F) == 0x0027`. *Confidence: High*
- **[buggy_axis.cpp › update, velocity check]** `history_.push_back()` allocates with unbounded growth, and `std::cout << std::endl` blocks and flushes, both inside the 1 kHz loop. **Why it matters:** latency spikes and cycle overruns, and memory grows without bound. **Fix:** use a preallocated ring buffer and a lock-free log queue drained by a non-RT thread. *Confidence: High*
- **[buggy_axis.cpp › update, dt]** `dt` comes from `system_clock`, which can jump when NTP or someone adjusts the clock. `dt` can then be zero or negative and is used as a divisor. The first cycle's `dt` is also measured from construction time. **Fix:** use `steady_clock`, check `dt` against the expected period (overrun detection), and skip velocity math if `dt <= 0`. *Confidence: High*

## Suggestions
- **[buggy_axis.cpp › update]** `last_target_rad_` only updates while enabled, so the first enabled cycle computes velocity from a stale value. Initialize it from the actual position at enable.
- **[buggy_axis.cpp › update]** Split the state machine, command shaping, and logging into separate functions. That makes each part unit-testable without a drive.

## Missing Tests
- Fault, then fault clears: the axis must stay disabled until an explicit reset and enable.
- Enable with actual position far from the last target: the commanded position must equal actual on the first enabled cycle.
- Setpoints of NaN, +Inf, and values past joint limits: no target outside limits is ever sent.
- A step setpoint larger than `max_vel * dt`: the output is rate-limited.
- A statusword with warning or target-reached bits set while Operation Enabled: targets keep flowing.
- Working-counter mismatch for N cycles: a fault is raised.
- Clock jump and `dt = 0`: no division by zero, overrun flagged.
- Concurrent `setTarget()` under a thread sanitizer (TSan) run.

## Cannot Verify From This Diff
- Whether the drive's own communication watchdog stops the axis on PDO loss.
- Whether `kCountsPerRad = 83443.0` and its sign match the encoder and gearbox.
- Quick-stop, limit-switch, and following-error handling elsewhere in the stack.
- Whether `update()` actually runs on an RT-priority thread with locked memory.
- Requirement and hazard links: none provided. Please link this change to the relevant hazard items (e.g., unintended motion).

## Nits
- `0x0008`, `0x0080`, `0x000F`, `0x0237` should be named constants.

## Positive Notes
- The comments state which thread calls each method, which made the race easy to spot.

---

## Review 2: clean_limits.cpp

**Mode:** Strict safety (position command clamp intended for the RT path)
**Scope reviewed:** clean_limits.cpp › clampPositionCommand

## Summary
A small, RT-safe position clamp with finite checks and validation of the limits themselves. No issues found.

## Safety Impact
This is on the actuator command path. The reviewed code rejects non-finite input and invalid limits, and clamps everything else.

## Critical Issues
None found.

## Major Issues
None found.

## Suggestions
None found.

## Missing Tests
- NaN, +Inf, and −Inf for the command and for each limit: returns `nullopt`.
- `min > max`: returns `nullopt` (this also guards against `std::clamp` UB).
- Values exactly at min and at max, and just outside each.

## Cannot Verify From This Diff
- That every caller actually treats `nullopt` as a fault and commands a controlled stop, as the comment says.
- That joint limits are loaded in radians.

## Nits
None.

## Positive Notes
- It guards `min > max` before calling `std::clamp`, avoiding undefined behavior.
- `noexcept`, no allocation, and no I/O match the RT-safety claim in the comment.

---

## Scorecard

| # | Planted bug | Caught | Severity given |
|---|---|---|---|
| 1 | Statusword compared without mask | Yes | Major |
| 2 | Heap allocation + `std::cout` in RT loop | Yes | Major |
| 3 | No NaN check or clamp on target | Yes | Critical |
| 4 | Working counter ignored | Yes | Major |
| 5 | Fault reset overwritten, auto re-enable after fault | Yes | Critical |
| 6 | Enable without setting target = actual (jump) | Yes | Critical |
| 7 | deg/s vs rad/s limit | Yes | Critical |
| 8 | `system_clock` for dt, divide by dt | Yes | Major |
| 9 | Data race on `target_rad_` | Yes | Major |

Additional real issues found beyond the planted ones: velocity limit detected but not enforced, skipped CIA402 transitions, `int32_t` cast UB, and stale `last_target_rad_`.
False positives on clean_limits.cpp: 0.
For comparison, the compiler with `-Wall -Wextra` flagged 1 of 9, and only indirectly (unused `working_counter`).
