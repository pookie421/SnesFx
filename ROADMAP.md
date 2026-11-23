# SnesFx roadmap

SnesFx aims to combine verified SNES behavior with a small, pure x86 assembly
core. The current milestone is a headless diagnostic emulator. It builds for
native x86-64 and 386-class protected mode and has passed cross-host diagnostic
checks in emulated and translated environments. See the [architecture](docs/ARCHITECTURE.md)
for implementation details and the exact verification matrix.

## 1. Make the diagnostic baseline reproducible

- Run the original diagnostic, PPU tests, and APU handshake in Linux x86-64 CI.
  Keep the x86-64 macOS and 386 BIOS replay checks reproducible with the same
  cartridge and instruction budget.
- Add a Windows runtime smoke test and retain the DOS boot recipe. Record the
  OS or guest image, CPU model, memory, and exact command for each result.
- Publish a coverage table that identifies each test, hardware subsystem,
  expected result, and the source of that expectation.
- Expand the CPU fixtures for decimal arithmetic, native/emulation mode,
  stack and bank wrapping, interrupt sampling, and ordered bus accesses.
  Add SPC700 vectors around timers, a full upload handshake, and port timing.

**Exit criteria** include a scripted local/CI run, matching cross-host
diagnostic summaries, and recorded first-mismatch information for timing
regressions. The ignored build directory holds generated ROMs and output.

## 2. Add real-time presentation and input

- Run the core in bounded guest-time slices with a host timer driving pacing.
  Add interactive keyboard and gamepad input, sampled at emulated controller
  latch or poll boundaries. Timestamp those samples for deterministic replay.
- Add an x86-64 presentation path for native frames and a DOS path suited to
  the supported VGA or VESA adapters. Keep aspect ratio, scaling, palette
  conversion, and dropped presentation frames in the host adapter.
- Persist battery SRAM with an atomic replacement strategy and test repeated
  load and save. Preserve the headless diagnostic mode so performance and
  timing can be measured with presentation costs included or excluded.

**Exit criteria** include playable input and continuous video for an original
diagnostic with no enhancement chip, stable guest pacing, and repeatable
guest results from the same input stream.

## 3. Complete console timing, graphics, and audio

- Build a scalar timing reference with ordered CPU bus phases, IRQ/NMI
  sampling, DMA and HDMA arbitration, WRAM refresh, and the observable
  controller, math, and APU register edges. Use explicit hardware revision and
  region profiles for measured differences.
- Add a PPU fetch-and-latch pipeline. Cover Modes 2 and 4 offset-per-tile,
  Modes 5 and 6 hires, mosaic, pseudo-hires, overscan, interlace, OAM and CGRAM
  access during rendering, and mid-scanline register changes. Record the
  first differing dot and hardware state for visual tests.
- Implement all 32 S-DSP phases, including BRR decode, Gaussian interpolation,
  envelopes, key timing, pitch modulation, noise, echo/FIR, and ARAM access
  ordering. Generate native stereo PCM. Keep host resampling and buffering in
  the runner so audio rate control cannot alter the guest audio clock.
- Define a versioned save-state format with explicit guest fields for pending
  events, in-progress operations, memory latches, PPU pipeline state, and
  audio oscillator remainder. Verify restore and replay at irregular cycle
  boundaries across the host builds.

**Exit criteria** include passing focused diagnostics for the supported
hardware profiles, matches against expected bus ordering and native pixels,
and native PCM checks before host resampling. Run behavior probes on
identified physical NTSC and PAL consoles. Reference emulators can help
isolate differences. Use physical measurements to resolve disagreements.

## 4. Establish the lowest full-speed PC configuration

- Use a 386DX-33, 486DX2-66, and Pentium-class machine as measurement points.
  Record the exact system, clock, cache, RAM, video and audio adapters, DOS
  version, and benchmark workload. Report boot, offline execution, and
  full-speed play as distinct results.
- Benchmark the scalar path by subsystem using fixed cartridge replays.
  Record frame-time percentiles, real-time ratio, dropped frames, audio
  underruns, and memory high-water mark. Include demanding raster, DMA, and
  audio fixtures as well as a simple cartridge.
- Profile plain-memory access and opcode dispatch, then streamline hot
  regions with direct mappings and compact specialization. Batch pixels
  only up to the first affected fetch, latch, or register boundary. Shrink
  renderer working sets for limited caches. Optional modern x86 SIMD can
  use the same guest-state contract.
- Compare every optimized path against the scalar reference with bus traces,
  native pixels, and PCM. An older PC may run the headless core or simulate
  offline even when its presentation cannot keep pace.

**Exit criteria** include sustained native-rate video and audio without
frameskip or underruns on a named physical configuration and test
workload. A 486DX2-66 is a performance probe, with roughly 1.1 million
host clocks per 60 Hz frame before video and audio I/O. Publish the measured
floor once the accuracy and performance gates can both be verified.

## 5. Expand cartridge coverage

- Extend cartridge detection and map each board's ROM, SRAM, firmware, and
  coprocessor I/O explicitly. Preserve session compatibility for LoROM,
  HiROM, and ExHiROM.
- Implement representative enhancement hardware in stages. Start with
  DSP-1, Super FX, and SA-1, then cover other boards based on test
  availability and measured behavior. Include each device's arbitration and
  clock-domain rules in the same trace and save-state contract.
- Track stage-by-stage compatibility with original or redistributable
  diagnostic cartridges, and document physical checks for representative
  revision/board combinations. Give each enhancement-chip class its own
  performance result on the host matrix.

**Exit criteria** include deterministic per-board diagnostics, stable
save-state replay, and published coverage tied to measured behavior.
