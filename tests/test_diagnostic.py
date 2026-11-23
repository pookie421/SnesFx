"""Verify the standalone emulator with the original diagnostic cartridge."""
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    emulator, rom, frame, *runner = sys.argv[1:]
    invocation = [*runner, str(Path(emulator).resolve()), rom]
    trace = Path(frame).with_suffix(".trace")
    probe = subprocess.run(
        [*invocation, "200000", frame, trace], capture_output=True, text=True, check=True
    )
    summary = probe.stdout.strip()
    assert re.fullmatch(
        r"ticks=[0-9a-f]{8}:[0-9a-f]{8} bus=[0-9a-f]{8} hash=[0-9a-f]{8}"
        r" frames=00000001 wram=50415353 fault=00000000 status=00000001",
        summary,
    ), summary
    records = list(struct.iter_unpack("<QII", trace.read_bytes()))
    assert len(records) == int(summary.split(" bus=")[1].split()[0], 16)
    assert all(a[0] <= b[0] for a, b in zip(records, records[1:]))
    first_dma = next(tick for tick, address, _ in records if address >> 24 == 2)
    writes = [tick for tick, address, _ in records if address == 0x03002180]
    irq_vector = next(tick for tick, address, _ in records if address == 0xffee)
    assert len(writes) == 512
    assert first_dma < 1364 + 16 * 4 + 14 < writes[-1] < irq_vector
    replay = subprocess.run(
        [*invocation, "200000"], capture_output=True, text=True, check=True
    )
    assert replay.stdout.strip() == summary, replay.stdout

    cartridge = Path(rom).read_bytes()
    with tempfile.TemporaryDirectory() as temporary_directory:
        candidate = Path(temporary_directory) / "diagnostic.sfc"
        for offset, mapping in ((0, None), (0x8000, 0x21), (0x408000, 0x25)):
            game = bytearray(offset) + cartridge
            if mapping is not None:
                game[offset + 0x7fd5] = mapping
            # The zero-offset variant verifies a 512-byte copier header.
            candidate.write_bytes(bytes(512) + game if offset == 0 else game)
            mapped = subprocess.run(
                [*runner, str(Path(emulator).resolve()), candidate, "200000"],
                capture_output=True, text=True, check=True,
            )
            assert mapped.stdout.strip() == summary, mapped.stdout

    header = b"P6\n256 224\n255\n"
    data = Path(frame).read_bytes()
    assert len(data) == len(header) + 256 * 224 * 3 and data.startswith(header)

    def pixel(x, y):
        position = len(header) + (y * 256 + x) * 3
        return data[position : position + 3]

    assert pixel(0, 0) == bytes(3)
    assert pixel(0, 20) == bytes.fromhex("000818")
    assert pixel(4, 20) == bytes.fromhex("ffe784")
    assert pixel(0, 24) == pixel(4, 20)

    short_run = subprocess.run([*invocation, "1"], capture_output=True, text=True)
    assert short_run.returncode == 4 and "status=00000003" in short_run.stdout
    invalid_cart = subprocess.run(
        [*runner, str(Path(emulator).resolve()), frame], capture_output=True, text=True
    )
    assert invalid_cart.returncode == 2, invalid_cart.stdout
    print(summary)


if __name__ == "__main__":
    main()
