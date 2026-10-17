#!/usr/bin/env python3
"""Arrange actual host-renderer captures into review sheets and a mood animation."""

import json
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/assets'))
import robot_art

MOODS = [
    'Idle',
    'Thinking',
    'Working',
    'Waiting',
    'Blocked',
    'Done',
    'Surprised',
    'Sleeping',
    'Happy',
    'Curious',
    'Excited',
    'Sad',
]


def card(source, title, size=280):
    with Image.open(source) as raw:
        # Review the avatar pixels. Host fixtures substitute text and display APIs.
        image = raw.crop((54, 78, 414, 378)).resize(
            (size, round(size * 5 / 6)), Image.Resampling.LANCZOS
        )
    result = Image.new('RGB', (size, size), '#000000')
    result.paste(image, (0, 6))
    draw = ImageDraw.Draw(result)
    draw.text(
        (size / 2, size - 26),
        title,
        font=ImageFont.load_default(size=17),
        fill='#CFD9E3',
        anchor='mm',
    )
    return result


def sheet(folder, kind, names, columns, destination):
    size, gap = 280, 12
    rows = (len(names) + columns - 1) // columns
    image = Image.new(
        'RGB', (columns * (size + gap) + gap, rows * (size + gap) + gap + 58), '#101923'
    )
    draw = ImageDraw.Draw(image)
    draw.text(
        (gap, 14), 'Pocket robot / ' + kind, font=ImageFont.load_default(size=24), fill='#E9EEF4'
    )
    for i, name in enumerate(names):
        image.paste(
            card(folder / f'{kind}-{i}.ppm', name),
            (gap + i % columns * (size + gap), 58 + gap + i // columns * (size + gap)),
        )
    image.save(destination)


def main():
    output = ROOT / '.build/evidence/avatar'
    folder = output / 'frames'
    sheet(folder, 'mood', MOODS, 4, output / 'moods.png')
    names = [f'{i:02d}  {name}' for i, (name, _) in enumerate(robot_art.expressions())]
    sheet(folder, 'expr', names, 5, output / 'expressions.png')
    with Image.open(folder / 'expr-0.ppm') as source:
        source.crop((80, 90, 388, 365)).resize((616, 550), Image.Resampling.LANCZOS).save(
            output / 'pocket-robot.png'
        )
    files = sorted(folder.glob('motion-*.ppm'), key=lambda p: int(p.stem.split('-')[1]))
    frames = [card(path, MOODS[i // 20], 360) for i, path in enumerate(files)]
    frames[0].save(
        output / 'motion.gif',
        save_all=True,
        append_images=frames[1:],
        duration=100,
        loop=0,
        disposal=2,
    )
    (output / 'catalog.json').write_text(
        json.dumps(
            {
                'evidence': 'Host renderer with substitute display, clock and input hardware',
                'moods': MOODS,
                'expressions': names,
            },
            indent=2,
        )
        + '\n'
    )
    print('Host-rendered avatar preview:', output)


if __name__ == '__main__':
    main()
