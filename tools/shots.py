#!/usr/bin/env python3
"""Turn the raw PPM dumps from host/preview.c into PNGs + the README sheets.

Pure standard library on purpose (zlib only): the game's screenshot pipeline
should not depend on Pillow being installed.
"""
import os
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise ValueError("%s: not a binary PPM" % path)
    fields, pos = [], 2
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1
    w, h, _ = fields
    return w, h, data[pos:pos + w * h * 3]


def write_png(path, w, h, rgb):
    # one filter byte (0) per scanline
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def sheet(paths, cols, gap, out):
    tiles = [read_ppm(p) for p in paths]
    if not tiles:
        return False
    tw, th, _ = tiles[0]
    rows = (len(tiles) + cols - 1) // cols
    W = cols * tw + (cols - 1) * gap
    H = rows * th + (rows - 1) * gap
    canvas = bytearray(b"\x14\x14\x14" * (W * H))
    for i, (w, h, rgb) in enumerate(tiles):
        ox = (i % cols) * (tw + gap)
        oy = (i // cols) * (th + gap)
        for y in range(h):
            dst = ((oy + y) * W + ox) * 3
            canvas[dst:dst + w * 3] = rgb[y * w * 3:(y + 1) * w * 3]
    write_png(out, W, H, bytes(canvas))
    return True


def main():
    prev = sys.argv[1] if len(sys.argv) > 1 else "prev"
    ppm = sorted(f for f in os.listdir(prev) if f.endswith(".ppm"))
    for f in ppm:
        src = os.path.join(prev, f)
        w, h, rgb = read_ppm(src)
        write_png(src[:-4] + ".png", w, h, rgb)
        print("  %s -> %s.png" % (f, f[:-4]))

    main_frames = ["01-title", "02-coins", "03-jump", "07-gate-1",
                   "06-approach", "04-turn", "08-fall", "05-over"]
    paths = [os.path.join(prev, n + ".ppm") for n in main_frames]
    if all(os.path.exists(p) for p in paths):
        sheet(paths, 4, 4, os.path.join(prev, "screens.png"))
        print("  wrote screens.png")

    corners = ["turn-L-d1300", "turn-L-d800", "turn-L-d400", "turn-L-d150",
               "turn-R-d1300", "turn-R-d800", "turn-R-d400", "turn-R-d150"]
    paths = [os.path.join(prev, n + ".ppm") for n in corners]
    if all(os.path.exists(p) for p in paths):
        sheet(paths, 4, 4, os.path.join(prev, "cornerseq.png"))
        print("  wrote cornerseq.png")

    fall = [os.path.join(prev, "fall-%02d.ppm" % i) for i in range(0, 6)]
    if all(os.path.exists(p) for p in fall):
        sheet(fall, 3, 4, os.path.join(prev, "fallseq.png"))
        print("  wrote fallseq.png")


if __name__ == "__main__":
    main()
