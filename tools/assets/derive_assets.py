#!/usr/bin/env python3
"""Build original local artwork, reusing output only when all inputs match."""

import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    inputs = hashlib.sha256()
    for name in ('derive_assets.py', 'gen_assets.py', 'robot_art.py'):
        inputs.update(Path(__file__).with_name(name).read_bytes())
    fingerprint = inputs.hexdigest()
    stamp = args.output.with_suffix('.stamp.json')
    if args.output.is_file() and stamp.is_file():
        try:
            if json.loads(stamp.read_text()) == {
                'inputs': fingerprint,
                'output': hashlib.sha256(args.output.read_bytes()).hexdigest(),
            }:
                print('Avatar assets unchanged.')
                return
        except (OSError, ValueError):
            pass
    import gen_assets

    args.output.parent.mkdir(parents=True, exist_ok=True)
    gen_assets.main(str(args.output))
    stamp.write_text(
        json.dumps(
            {
                'inputs': fingerprint,
                'output': hashlib.sha256(args.output.read_bytes()).hexdigest(),
            }
        )
        + '\n'
    )


if __name__ == '__main__':
    main()
