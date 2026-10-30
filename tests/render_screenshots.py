"""Regenerate README PNGs with the actual game renderer (Python stdlib + clang)."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))


def main():
    with tempfile.TemporaryDirectory(prefix="ghostbusters-screenshots-") as temp:
        executable = Path(temp) / "validate_display"
        preview = Path(temp) / "preview.ppm"
        subprocess.run([
            os.environ.get("CC", "clang"), "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-Iapp_xghostbusters/src", "tests/validate_display.c",
            "app_xghostbusters/src/game.c", "-o", str(executable),
        ], cwd=ROOT, check=True)
        subprocess.run([str(executable), str(preview)], check=True)
        magic, dimensions, maximum, pixels = preview.read_bytes().split(b"\n", 3)
        if (magic, dimensions, maximum) != (b"P6", b"2400 320", b"255"):
            raise ValueError("Unexpected renderer preview format")
        if len(pixels) != 2400 * 320 * 3:
            raise ValueError("Incomplete renderer preview")

        destination = ROOT / "docs" / "images"
        destination.mkdir(parents=True, exist_ok=True)
        names = ("title", "gameplay", "game-over", "stay-puft", "traps",
                 "slimer", "hotel", "subway", "rooftops", "slimed")
        for panel, name in enumerate(names):
            rows = b"".join(
                b"\0" + pixels[(y * 2400 + panel * 240) * 3:
                                (y * 2400 + panel * 240 + 240) * 3]
                for y in range(320)
            )
            png = (b"\x89PNG\r\n\x1a\n"
                   + chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 320, 8, 2, 0, 0, 0))
                   + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
            path = destination / (name + ".png")
            path.write_bytes(png)
            print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
