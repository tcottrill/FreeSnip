#!/usr/bin/env python3
"""Builds packaging/icons: FreeSnip's icon, from the artwork in packaging/icons/freesnip.png.

usage: make-icons.py [OUT_DIR]     default: packaging/icons

The artwork (blue scissors cutting into crop corners, on a transparent background) is
trimmed to its visible pixels, centred on a square with a small margin and scaled down
to each size with Lanczos, plus light sharpening below 48 px so the small sizes keep
their edges. The program draws its own window, tray and About icons from the 256 px
PNG, compiled in by CMakeLists.txt (appIconImage in src/wx/image.cpp).

- hicolor/<n>x<n>/apps/io.github.tcottrill.FreeSnip.png for the Linux icon theme sizes.
- freesnip.ico for Windows (the exe, installer and shortcuts): classic 32-bit bitmaps up
  to 128 px and PNG only for 256 px, the one size every Windows icon loader accepts
  compressed (as tools/make-icons.py in Skein Downloader writes it).
Needs Pillow. Run it again whenever the artwork changes, then rebuild.
"""
import io
import pathlib
import struct
import sys

from PIL import Image, ImageFilter

APP_ID = 'io.github.tcottrill.FreeSnip'
SOURCE = pathlib.Path(__file__).resolve().parent.parent / 'packaging' / 'icons' / 'freesnip.png'
LINUX_SIZES = (16, 22, 24, 32, 48, 64, 96, 128, 192, 256, 512)
WINDOWS_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
MARGIN = 0.03   # of the side, on each edge


def master():
    art = Image.open(SOURCE).convert('RGBA')
    art = art.crop(art.getchannel('A').getbbox())
    side = round(max(art.size) / (1 - 2 * MARGIN))
    square = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    square.alpha_composite(art, ((side - art.width) // 2, (side - art.height) // 2))
    return square


def icon(art, s):
    image = art.resize((s, s), Image.Resampling.LANCZOS)
    if s < 48:
        image = image.filter(ImageFilter.UnsharpMask(radius=0.6, percent=60, threshold=0))
    return image


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
    art = master()
    for size in LINUX_SIZES:
        target = out / 'hicolor' / f'{size}x{size}' / 'apps' / f'{APP_ID}.png'
        target.parent.mkdir(parents=True, exist_ok=True)
        icon(art, size).save(target, optimize=True)
    write_ico([(size, icon(art, size)) for size in WINDOWS_SIZES], out / 'freesnip.ico')
    print('Linux sizes:', ', '.join(map(str, LINUX_SIZES)))
    print('Windows .ico sizes:', ', '.join(map(str, WINDOWS_SIZES)))


if __name__ == '__main__':
    main(*sys.argv[1:])
