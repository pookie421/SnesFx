# Testing SnesFx

Run these commands from the repository root. The builds use NASM 2.16 or later and Make. The integration checks use Python 3. Running the native fixtures requires x86-64 Linux or macOS with Intel support through the hardware or Rosetta. The BIOS comparison also requires `qemu-system-i386`.

## Reproduce the checks

```sh
make all test test-apu
make test-boot
```

Set `NASM=/path/to/nasm`, `PYTHON=/path/to/python3`, or `QEMU=/path/to/qemu-system-i386` on the relevant `make` command to select a tool outside `PATH`. Set `OUT=build/check` to keep a build isolated. The committed diagnostic and IPL fixtures are assembled locally from source.

| Check | What it verifies |
| --- | --- |
| `make all` | Assembles Linux ELF, macOS Mach-O, Windows PE, DOS MZ, and the bootable 386 BIOS image. |
| `make test` | Boots the original LoROM fixture and checks CPU arithmetic, DMA writes, interrupt ordering during DMA, mapping variants, trace ordering, determinism, error paths, and selected pixels of its Mode 0 frame. Includes the PPU checks. |
| `make test-ppu` | Runs isolated PPU assertions for VRAM, CGRAM and OAM ports, tile and sprite pixels, brightness, windows, color math, Mode 3, Mode 7, and selected counter and status behavior. A nonzero exit code identifies a failing assertion in the source. |
| `make test-apu` | Builds with an original 64-byte diagnostic IPL. Verifies two-way CPU–SPC700 port communication, an SPC700 timer, and the base diagnostic checks. Uses its own output directory for the IPL-enabled runner. |
| `make test-boot` | Boots the BIOS image in QEMU with a 486 CPU model and 2 MiB of RAM. The harness compares the summary with the native runner for the same ROM and instruction budget. |

[Integration harness](../tests/test_diagnostic.py) · [PPU assertions](../tests/ppu_test.asm) · [Diagnostic IPL](../tests/apu_ipl.asm) · [BIOS comparison](../tests/test_boot.py)

The diagnostic should terminate with `frames=00000001 wram=50415353 fault=00000000 status=00000001`. `50415353` is the fixture's ASCII `PASS` signature. The Python harness checks the order of the DMA and IRQ events and requires a trace-free replay to print the same summary. An isolated change can legitimately alter the tick count or bus hash when it corrects behavior. Record the evidence for the first event that moved.

To run the BIOS comparison with the audio CPU enabled, reuse the generated fixture and keep the resulting runners separate from the base build.

```sh
make test-apu
make OUT=build/boot-apu IPL=build/test-ipl.bin ROM=build/diagnostic-apu.sfc test-boot
```

## Capture a trace

On x86-64 Linux, run the stock diagnostic with a fixed instruction budget.

```sh
make linux build/diagnostic.sfc
build/snes-linux build/diagnostic.sfc 200000 build/frame.ppm build/trace.bin
```

On macOS, build `macos` in place of `linux` and invoke `build/snes-macos`. On Apple Silicon, prefix the invocation with `arch -x86_64`. The PPM is a 256×224 RGB frame. Use `-` for the frame argument when capturing only the trace.

Each binary trace record uses little-endian `<QII`. The first field is the guest master tick. The second holds the 24-bit address and an eight-bit tag in the high byte. Tags `0`, `1`, `2`, and `3` indicate CPU read, CPU write, DMA read, and DMA write, respectively. The third field holds the byte value. The file contains up to the first 65,536 bus events. The summary's count and rolling hash cover all recorded bus activity, including events after that limit.

```sh
python3 - <<'PY'
from pathlib import Path
from struct import iter_unpack

trace = Path('build/trace.bin').read_bytes()
for tick, tagged_address, value in iter_unpack('<QII', trace):
    tag = tagged_address >> 24
    address = tagged_address & 0xffffff
    print(f'{tick:12d} {tag} ${address:06x} ${value:02x}')
PY
```

## Reproduce the screenshots

- **Diagnostic frame.** Export `build/frame.ppm` with the command above.
  Convert it to PNG at its original 256 × 224 size. On macOS, run
  `sips -s format png build/frame.ppm --out build/diagnostic-frame.png`.
  [Repository capture](images/diagnostic-frame.png)
- **BIOS diagnostic.** Boot the image with the 486 CPU model and 2 MiB RAM.
  Once the VGA text shows `wram=50415353`, use QEMU's `screendump` monitor
  command to capture the screen to PPM. The repository image crops away the
  blank rows below the two lines of output. [Repository capture](images/boot-486.png)

## Verification status

The macOS runner passed the integration, PPU, and SPC700 diagnostic checks through Rosetta. The x86-64 Linux runner was exercised in QEMU, where the base diagnostic's PPM and trace matched the macOS run byte for byte. The BIOS image matched the native summary for the base and SPC700 diagnostics in QEMU. The DOS MZ runner matched the base summary under FreeDOS and CWSDPMI in QEMU. The Windows PE was verified at build and file-format level.

The base and SPC700 integration runs produced these summaries with NASM 2.16.03.

```text
ticks=00000000:0004afd4 bus=000088ad hash=9508479c frames=00000001 wram=50415353 fault=00000000 status=00000001
ticks=00000000:0004b1f0 bus=000088e7 hash=154faab4 frames=00000001 wram=50415353 fault=00000000 status=00000001
```

During implementation, a separate flat-memory test harness sampled 31,680 65C816 vectors and 26,086 SPC700 vectors. The 65C816 agreed on 31,507 vectors. Two emulation-mode cases differ on the documented `(direct,X)` high-byte page wrap and 171 WDM cases differ on the signature byte's bus activity. The WDM behavior needs a hardware measurement. The SPC700 agreed on all 26,086 samples, and separate cases exercised SLEEP and STOP. These model comparisons are developer test evidence. The repository's Make targets run the original fixtures listed above. [65C816 vectors](https://github.com/SingleStepTests/65816), [SPC700 vectors](https://github.com/SingleStepTests/spc700), [SNES CPU test ROM notes](https://github.com/gilyon/snes-tests/tree/main/cputest)

Physical SNES conformance and real-time performance on physical 386 or 486 hardware remain unverified. When reporting a hardware run, include the SNES region and board or chip revision if known. For CPU host benchmarks, include the actual processor, clock, memory, video settings, and the measurement method. Keep third-party game, firmware, and IPL images out of public issue attachments.
