"""Render documented LCD previews from the firmware's shared C++ view model.

These are layout previews (not photos or measured charging evidence). The
normal view shows a provisional voltage fixture and unavailable current.
Font rasterization uses Pillow; coordinates, text, sizes and colors are shared
with the actual firmware. Requires Pillow, available with ESPHome.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw, ImageFont
import yaml

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk')
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/images')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='lxy-lcd-render-') as work:
        binary = str(Path(work) / 'render')
        command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror']
        if args.sdk:
            command += ['-isysroot', args.sdk]
        subprocess.run(command + [str(ROOT / 'tests/render_lcd.cpp'), '-o', binary], check=True)
        lines = subprocess.check_output([binary], text=True).splitlines()
    declared = set(yaml.safe_load((ROOT / 'assets/ui-glyphs.yaml').read_text()))
    labels = (ROOT / 'components/charger_display/charger_display.h').read_text()
    missing = {c for c in labels if ord(c) > 127} - declared
    if missing:
        raise RuntimeError(f'UI font inventory missing characters: {sorted(missing)}')
    fonts = [ImageFont.truetype(str(ROOT / 'assets/ChargerSansSC.ttf'), n) for n in (12, 14)] + [ImageFont.truetype(str(ROOT / 'assets/Roboto.ttf'), n) for n in (40, 76, 28)]
    colors = [(255, 255, 255), (160, 160, 160), (64, 230, 140), (255, 185, 64)]
    names = []
    args.output.mkdir(parents=True, exist_ok=True)
    pages = []
    for line in lines:
        parts = line.split('\t')
        if parts[0] == 'PAGE':
            names.append(parts[1])
            pages.append(Image.new('RGB', (240, 135), 'black'))
            continue
        x, y, font, ink, right = map(int, parts[:5])
        text = parts[5]
        # ESPHome TOP text uses the font's top line box, not the glyph's ink box.
        draw = ImageDraw.Draw(pages[-1])
        anchor = 'ra' if right else 'la'
        box = draw.textbbox((x, y), text, font=fonts[font], anchor=anchor)
        if box[0] < 0 or box[1] < 0 or box[2] > 240 or box[3] > 135:
            raise RuntimeError(f'Text outside 240x135 screen: {text!r}: {box}')
        draw.text((x, y), text, font=fonts[font], fill=colors[ink], anchor=anchor)
    for name, page in zip(names, pages, strict=True):
        page.resize((960, 540), Image.Resampling.NEAREST).save(args.output / name)
        print(name)


if __name__ == '__main__':
    main()
