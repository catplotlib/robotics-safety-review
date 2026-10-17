# Robotics Safety Review — a Claude skill

A code-review skill for Claude that produces structured, specific, honest reviews, with a **strict safety mode** for real-time robotics and safety-critical control code.

Most AI code reviewers are built for web apps. This one also knows what breaks robots: allocations in a 1 kHz control loop, CIA402 statuswords compared without masks, actuator commands that skip limit clamps, fault paths that never reach a safe state.

## What it checks

**Every review**
- Bugs and logic errors, edge cases, error handling
- Security, resources, performance
- Breaking API changes, missing tests

**Strict safety mode** (auto-enabled for control, motion, drive, RT, ROS 2, medical code)
- Real-time determinism: no heap, no blocking, no exceptions, monotonic clocks
- Fault handling: every error path reaches a safe state; no auto re-enable
- Numerics: command clamping, NaN/Inf, units, overflow, encoder wraparound
- Concurrency: races, torn reads, ISR safety
- EtherCAT / CIA402: state machine, fault reset edges, working counter, enable without jump
- ROS 2: executor starvation, QoS, stale timestamps, lifecycle
- C/C++ undefined behavior; MISRA / AUTOSAR where used
- Traceability: links findings to hazards/DFMEA items when provided

## Output

Every review uses the same structure: Summary → Safety Impact → Critical → Major → Suggestions → Missing Tests → Cannot Verify → Nits → Positive Notes. Each finding has a location, the runtime consequence, a fix, and a confidence level.

## Design principles

- **No invented issues.** Findings must point to visible code; anything else goes under "Cannot verify from this diff".
- **Never claims compliance.** It supports, not replaces, formal review and verification.
- **Consequences over rules.** Every comment says what the robot actually does wrong.

## Test results

On a C++ axis controller with 9 planted bugs, the skill caught all 9 plus 4 real extra issues. On a clean file, it reported zero false positives. For comparison, `g++ -Wall -Wextra` flagged 1 of the 9, and only indirectly. The inputs and full reviews are in [`tests/`](tests/test-results.md).

These are author-written tests. Independent runs on real-world bugs are welcome; please open an issue with what it caught or missed.

## Install

**Claude Code**
```bash
git clone https://github.com/catplotlib/robotics-safety-review.git
cp -r robotics-safety-review/robotics-safety-review ~/.claude/skills/
```
Then ask Claude to review a diff, or run `/robotics-safety-review`.

**Claude apps (web/desktop):**
1. Turn on code execution and file creation in Settings › Capabilities.
2. Zip the inner `robotics-safety-review/` folder (the one containing `SKILL.md`).
3. Go to Customize › Skills › **+** › Create skill › Upload a skill, and choose the zip.

## Usage

```
Review this PR: <paste diff>
Review drive_manager.cpp in strict mode. Hazard IDs: HZ-12 (unintended motion), HZ-19 (loss of e-stop).
```

## Disclaimer

This is a review aid. It is not a static analyzer, not a certification tool, and not a substitute for your organization's IEC 62304 / ISO 14971 / ISO 26262 process. Don't paste proprietary code into AI tools without your employer's approval.

## License

MIT
