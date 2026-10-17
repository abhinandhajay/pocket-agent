"""Generate local robot artwork and test the real firmware renderer on the host."""

import argparse
import math
import os
from pathlib import Path
import subprocess
import sys

from support import Checks, load

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview', action='store_true')
    args = parser.parse_args()
    check = Checks()
    art = load('robot_art', ROOT / 'tools/assets/robot_art.py')
    definitions = art.expressions()
    check(len(definitions) == 25, 'all expression IDs preserved')
    for name, pair in definitions:
        check(len(pair) == 2, name + ': eye pair')
        for eye in pair:
            check(len(eye) == 64, name + ': contour resolution')
            check(all(math.isfinite(x) and math.isfinite(y) for x, y in eye), name + ': finite')
            check(
                all(art.visor_distance(x - art.CENTER, y - art.CENTER) < -10 for x, y in eye),
                name + ': eye fits inside visor with gaze clearance',
            )
    build = Path(os.environ.get('POCKET_AGENT_BUILD', ROOT / '.build')) / 'avatar-test'
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            sys.executable,
            str(ROOT / 'tools/assets/derive_assets.py'),
            str(build / 'avatar_assets.h'),
        ],
        check=True,
    )
    binary = build / 'check-avatar'
    subprocess.run(
        [
            os.environ.get('CXX', 'c++'),
            '-std=c++17',
            '-O1',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-Wno-unused-function',
            '-Wno-unused-variable',
            '-fsanitize=address,undefined',
            '-I' + str(build),
            '-I' + str(ROOT / 'firmware/pocket_agent'),
            str(ROOT / 'tests/check_avatar.cpp'),
            str(ROOT / 'firmware/pocket_agent/touch_calibration_core.cpp'),
            '-o',
            str(binary),
        ],
        check=True,
    )
    command = [str(binary)]
    if args.preview:
        command.append(str(ROOT / '.build/evidence/avatar/frames'))
    subprocess.run(command, check=True)
    print(f'Original robot geometry: {check.count} checks passed')


if __name__ == '__main__':
    main()
