#!/usr/bin/env python3
"""Builds packaging/icons: Snip's icon, a blue rounded square with white crop corners.

usage: make-icons.py [OUT_DIR]     default: packaging/icons

The geometry is the same as appIconImage() in src/wx/image.cpp, which draws the window
and tray icons at run time: corner radius s/5, marks inset 22%; from 48 px up arms of
19% and 8% thick, smaller sizes longer (28%) and thicker (s/10) marks so they stay
readable. Each size is drawn at 4x with its marks on whole pixels and scaled down, so
only the rounded corners are smoothed.

- hicolor/<n>x<n>/apps/io.github.tcottrill.Snip.png for the Linux icon theme sizes.
- snip.ico for Windows (the exe, installer and shortcuts): classic 32-bit bitmaps up
  to 128 px and PNG only for 256 px, the one size every Windows icon loader accepts
  compressed (as tools/make-icons.py in Skein Downloader writes it).
Needs Pillow. Run it again whenever the design changes, then rebuild.
"""
import io
import pathlib
import struct
import sys

from PIL import Image, ImageDraw

APP_ID = 'io.github.tcottrill.Snip'
LINUX_SIZES = (16, 22, 24, 32, 48, 64, 96, 128, 192, 256, 512)
WINDOWS_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
BLUE = (0x25, 0x63, 0xEB, 255)
WHITE = (255, 255, 255, 255)
SCALE = 4


def icon(s):
    k = SCALE
    image = Image.new('RGBA', (s * k, s * k), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, s * k - 1, s * k - 1), radius=s * k / 5, fill=BLUE)
    large = s >= 48
    inset = s * 22 // 100
    length = max(2, s * (19 if large else 28) // 100)
    thick = max(1, s * 8 // 100 if large else s // 10)
    lo, hi = inset, s - inset

    def fill(x, y, w, h):
        draw.rectangle((x * k, y * k, (x + w) * k - 1, (y + h) * k - 1), fill=WHITE)
    fill(lo, lo, length, thick);                  fill(lo, lo, thick, length)
    fill(hi - length, lo, length, thick);         fill(hi - thick, lo, thick, length)
    fill(lo, hi - thick, length, thick);          fill(lo, hi - length, thick, length)
    fill(hi - length, hi - thick, length, thick); fill(hi - thick, hi - length, thick, length)
    return image.resize((s, s), Image.Resampling.LANCZOS)


def dib(image):
    """A 32-bit BGRA bitmap entry with its AND mask, as icon resources store them."""
    w, h = image.size
    header = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b, a = image.getpixel((x, y))
            row += bytes((b, g, r, a))
        rows.append(bytes(row))
    mask_stride = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        bits = bytearray(mask_stride)
        for x in range(w):
            if image.getpixel((x, y))[3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    return header + b''.join(rows) + bytes(mask)


def write_ico(images, path):
    entries, blobs = [], []
    offset = 6 + 16 * len(images)
    for size, image in images:
        if size >= 256:
            buffer = io.BytesIO(); image.save(buffer, 'PNG'); blob = buffer.getvalue()
        else:
            blob = dib(image)
        entries.append(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(blob), offset))
        blobs.append(blob)
        offset += len(blob)
    path.write_bytes(struct.pack('<HHH', 0, 1, len(images)) + b''.join(entries) + b''.join(blobs))


def main(out='packaging/icons'):
    out = pathlib.Path(out)
    for size in LINUX_SIZES:
        target = out / 'hicolor' / f'{size}x{size}' / 'apps' / f'{APP_ID}.png'
        target.parent.mkdir(parents=True, exist_ok=True)
        icon(size).save(target)
    write_ico([(size, icon(size)) for size in WINDOWS_SIZES], out / 'snip.ico')
    print('Linux sizes:', ', '.join(map(str, LINUX_SIZES)))
    print('Windows .ico sizes:', ', '.join(map(str, WINDOWS_SIZES)))


if __name__ == '__main__':
    main(*sys.argv[1:])
