#!/usr/bin/env python3
"""Screenshot processing and management helper for Metroid Zero Mission 3DS.

This script helps manage screenshots for the repository README:
1. Generates placeholders when no screenshots are provided.
2. Converts raw hardware/emulator dumps (mzm-dump-*.rgb) to rotated, color-corrected PNGs.
3. Converts Luma3DS screenshots (top_*.bmp/png, bot_*.bmp/png) into the screenshots/ directory.
4. Generates a composite dual-screen preview image (preview.png).

Usage:
    python3 tools/process_screenshots.py --placeholders
    python3 tools/process_screenshots.py --import <directory_with_dumps_or_screenshots>
    python3 tools/process_screenshots.py --convert-dump <file.rgb> <output.png> [top|bottom]
"""

import argparse
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SCREENSHOTS_DIR = os.path.join(ROOT, "screenshots")

TOP_W, TOP_H = 400, 240
BOT_W, BOT_H = 320, 240
DUMP_PORTRAIT_W = 240


def encode_png(width, height, rgb_bytes):
    """Encodes raw RGB8 bytes (width * height * 3) into PNG bytes without external dependencies."""
    header = b"\x89PNG\r\n\x1a\n"
    ihdr_data = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    ihdr_chunk = b"IHDR" + ihdr_data
    ihdr = struct.pack(">I", len(ihdr_data)) + ihdr_chunk + struct.pack(">I", zlib.crc32(ihdr_chunk) & 0xFFFFFFFF)

    raw = bytearray()
    row_bytes = width * 3
    for y in range(height):
        raw.append(0)  # Filter type 0 (None)
        start = y * row_bytes
        raw.extend(rgb_bytes[start : start + row_bytes])

    compressed = zlib.compress(bytes(raw), 9)
    idat_chunk = b"IDAT" + compressed
    idat = struct.pack(">I", len(compressed)) + idat_chunk + struct.pack(">I", zlib.crc32(idat_chunk) & 0xFFFFFFFF)

    iend_chunk = b"IEND"
    iend = struct.pack(">I", 0) + iend_chunk + struct.pack(">I", zlib.crc32(iend_chunk) & 0xFFFFFFFF)

    return header + ihdr + idat + iend


def convert_rgb_dump(raw_bytes, is_top=True):
    """Converts a native 3DS hardware dump (portrait BGR8) to landscape RGB8."""
    target_w = TOP_W if is_top else BOT_W
    target_h = 240
    expected_sz = DUMP_PORTRAIT_W * target_w * 3
    if len(raw_bytes) != expected_sz:
        raise ValueError(f"Expected {expected_sz} bytes, got {len(raw_bytes)}")

    landscape = bytearray(target_w * target_h * 3)
    for y in range(target_h):
        for x in range(target_w):
            src_px = DUMP_PORTRAIT_W - 1 - y
            src_py = x
            src_off = (src_py * DUMP_PORTRAIT_W + src_px) * 3
            dst_off = (y * target_w + x) * 3
            # 3DS dump is BGR8; convert to RGB8
            landscape[dst_off] = raw_bytes[src_off + 2]
            landscape[dst_off + 1] = raw_bytes[src_off + 1]
            landscape[dst_off + 2] = raw_bytes[src_off]
    return bytes(landscape), target_w, target_h


def create_placeholder_image(width, height, title, subtitle):
    """Creates a clean placeholder PNG with border, background and label."""
    try:
        from PIL import Image, ImageDraw
        im = Image.new("RGB", (width, height), (15, 23, 42))  # slate-900
        draw = ImageDraw.Draw(im)
        draw.rectangle([(2, 2), (width - 3, height - 3)], outline=(56, 189, 248), width=2)
        draw.rectangle([(6, 6), (width - 7, height - 7)], outline=(30, 41, 59), width=1)
        draw.text((width // 2, height // 2 - 14), title, fill=(241, 245, 249), anchor="mm")
        draw.text((width // 2, height // 2 + 14), subtitle, fill=(148, 163, 184), anchor="mm")
        # Save to buffer
        import io
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        return buf.getvalue()
    except Exception:
        # Fallback pure-python rasterizer
        buf = bytearray(width * height * 3)
        bg = (15, 23, 42)
        border = (56, 189, 248)
        for y in range(height):
            for x in range(width):
                off = (y * width + x) * 3
                if x < 3 or x >= width - 3 or y < 3 or y >= height - 3:
                    buf[off : off + 3] = border
                else:
                    buf[off : off + 3] = bg
        return encode_png(width, height, bytes(buf))


def generate_composite_preview(top_png_path, bot_png_path, out_path):
    """Creates a dual-screen preview image showing top and bottom screens stacked."""
    try:
        from PIL import Image
        top_im = Image.open(top_png_path)
        bot_im = Image.open(bot_png_path)
        
        # Dimensions: 400 wide, (240 + 20 gap + 240 = 500) high
        comp_w, comp_h = 400, 500
        comp = Image.new("RGB", (comp_w, comp_h), (11, 15, 25))
        
        # Center top screen at y=10
        comp.paste(top_im, (0, 10))
        # Center bottom screen (320 wide) at x=40, y=260
        comp.paste(bot_im, ((comp_w - bot_im.width) // 2, 255))
        
        comp.save(out_path)
        print(f"Generated dual-screen preview: {out_path}")
        return True
    except Exception as e:
        print(f"Composite generation skipped (PIL required): {e}")
        return False


def generate_all_placeholders():
    os.makedirs(SCREENSHOTS_DIR, exist_ok=True)
    placeholders = [
        ("top_gameplay_wide.png", TOP_W, TOP_H, "Top Screen: Widescreen Mode", "400x240 - Wide (16:9) Gameplay"),
        ("top_gameplay_bezel.png", TOP_W, TOP_H, "Top Screen: Pixel Perfect + Bezel", "400x240 - GBA Frame & LCD Grid"),
        ("bottom_map.png", BOT_W, BOT_H, "Bottom Screen: MAP Tab", "320x240 - Real-time Map & Counters"),
        ("bottom_status.png", BOT_W, BOT_H, "Bottom Screen: STATUS Tab", "320x240 - Equipment & Abilities"),
        ("bottom_states.png", BOT_W, BOT_H, "Bottom Screen: STATE Tab", "320x240 - Save States & Thumbnails"),
        ("bottom_achievements.png", BOT_W, BOT_H, "Bottom Screen: ACHIEVEMENTS Tab", "320x240 - RetroAchievements Badges"),
        ("bottom_options.png", BOT_W, BOT_H, "Bottom Screen: OPTIONS Tab", "320x240 - Display & Controls Config"),
    ]

    for fname, w, h, title, sub in placeholders:
        target = os.path.join(SCREENSHOTS_DIR, fname)
        # Only create if it doesn't already exist or is a placeholder
        png_bytes = create_placeholder_image(w, h, title, sub)
        with open(target, "wb") as f:
            f.write(png_bytes)
        print(f"Wrote placeholder: {target}")

    # Also composite preview
    top_p = os.path.join(SCREENSHOTS_DIR, "top_gameplay_wide.png")
    bot_p = os.path.join(SCREENSHOTS_DIR, "bottom_map.png")
    prev_p = os.path.join(SCREENSHOTS_DIR, "preview.png")
    generate_composite_preview(top_p, bot_p, prev_p)


def import_directory(src_dir):
    """Scans src_dir for dumps (*.rgb) or Luma3DS screenshots (*.png, *.bmp)."""
    if not os.path.isdir(src_dir):
        print(f"Directory not found: {src_dir}", file=sys.stderr)
        return False

    os.makedirs(SCREENSHOTS_DIR, exist_ok=True)
    files = sorted(os.listdir(src_dir))
    imported = 0

    for f in files:
        fpath = os.path.join(src_dir, f)
        if f.endswith("-left.rgb"):
            # Top screen dump
            print(f"Converting top screen dump {f}...")
            with open(fpath, "rb") as rf:
                raw = rf.read()
            rgb, w, h = convert_rgb_dump(raw, is_top=True)
            out = os.path.join(SCREENSHOTS_DIR, f.replace("-left.rgb", "_top.png"))
            with open(out, "wb") as wf:
                wf.write(encode_png(w, h, rgb))
            imported += 1
        elif f.endswith("-bottom.rgb"):
            # Bottom screen dump
            print(f"Converting bottom screen dump {f}...")
            with open(fpath, "rb") as rf:
                raw = rf.read()
            rgb, w, h = convert_rgb_dump(raw, is_top=False)
            out = os.path.join(SCREENSHOTS_DIR, f.replace("-bottom.rgb", "_bottom.png"))
            with open(out, "wb") as wf:
                wf.write(encode_png(w, h, rgb))
            imported += 1

    print(f"Import finished. {imported} files processed.")
    return True


def main():
    parser = argparse.ArgumentParser(description="Process and manage MZM 3DS screenshots.")
    parser.add_argument("--placeholders", action="store_true", help="Generate initial placeholder images in screenshots/")
    parser.add_argument("--import-dir", dest="import_dir", help="Import screenshots or dumps from a directory")
    parser.add_argument("--composite", action="store_true", help="Re-generate preview.png composite")

    args = parser.parse_args()

    if args.import_dir:
        import_directory(args.import_dir)
        top_p = os.path.join(SCREENSHOTS_DIR, "top_gameplay_wide.png")
        bot_p = os.path.join(SCREENSHOTS_DIR, "bottom_map.png")
        prev_p = os.path.join(SCREENSHOTS_DIR, "preview.png")
        if os.path.exists(top_p) and os.path.exists(bot_p):
            generate_composite_preview(top_p, bot_p, prev_p)
    elif args.composite:
        top_p = os.path.join(SCREENSHOTS_DIR, "top_gameplay_wide.png")
        bot_p = os.path.join(SCREENSHOTS_DIR, "bottom_map.png")
        prev_p = os.path.join(SCREENSHOTS_DIR, "preview.png")
        generate_composite_preview(top_p, bot_p, prev_p)
    else:
        generate_all_placeholders()


if __name__ == "__main__":
    main()
