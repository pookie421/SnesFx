# Contributing to SnesFx

SnesFx is a headless SNES emulator written in NASM. The Linux, macOS, Windows, DOS, and BIOS runners share the same emulation core. See [Testing](docs/TESTING.md) for the build commands, fixture coverage, and verification status.

## Report a bug

1. Search the open issues for the symptom, hardware feature, or register involved. Add a reproduction to an existing issue if it describes the same behavior.
2. Include the SnesFx commit ID, runner, CPU and operating system, NASM version, and exact build and run commands. Specify whether execution used physical hardware, Rosetta, or a virtual machine. For the optional SPC700 build, identify the IPL by SHA-256 and describe its origin.
3. Describe the observed and expected behavior, the shortest instruction budget that reproduces it, and the emulator's exit code and summary line. For timing failures, include the first differing event's tick, address, bus owner, and value. A cropped PPM or a short trace excerpt from an original diagnostic fixture can help isolate the problem.
4. Use an original minimal test ROM when possible. Keep third-party ROMs, firmware, IPL dumps, save data, and traces containing their bytes local. Share a source link, title, region, revision, or checksum where those identifiers are useful. Use original source and assets for new fixtures.

## Open a pull request

1. Branch from the repository's default branch and keep the change focused. Say which hardware behavior or host runner the change affects. Point to the related issue or technical source when available.
2. Keep the emulator runtime in NASM. Follow the register and timing conventions in the shared core, and keep host-specific code in the corresponding runner. A shared change must assemble for both 32-bit and 64-bit x86.
3. Add a small regression when existing fixtures miss the behavior being fixed. For hardware timing changes, include the evidence for the expected result. Distinguish console measurements, published hardware tests, and differential results from another emulator or an instruction model.
4. Run the checks in [Testing](docs/TESTING.md). Record the platform and execution environment for each result. Include the before and after summary or the smallest relevant trace excerpt when an observable changes.

## Changes to timing and performance

Preserve the tick, order, address, direction, and value of guest-visible bus operations unless the change corrects one of those observables. Preserve the effects of bus ownership, memory wait states, interrupt sampling, and device register side effects. Check an optimized path against the existing path using identical input and an instruction budget that covers the behavior being optimized. A final screenshot can catch rendering regressions. Compare traces as well when the change can affect timing.

For performance claims, give the host CPU, clock, RAM, runner, build options, workload, and measurement method. Report guest ticks processed per wall-clock second or wall-clock runtime for a fixed guest workload. Include multiple runs and separate the timing cost of trace or frame export from core execution.
