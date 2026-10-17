#!/usr/bin/env python3
"""Bake original local artwork into firmware textures using only standard Python."""

import math
from pathlib import Path
import sys

import robot_art

N, LO, HI, QUANT = 128, -12.0, 268.0, 4.0
STEP = (HI - LO) / (N - 1)


def quantize(distance):
    return max(-127, min(127, round(distance * QUANT)))


def polygon_distance(points, x, y):
    nearest, inside = float('inf'), False
    previous = points[-1]
    for current in points:
        ax, ay = previous
        bx, by = current
        dx, dy = bx - ax, by - ay
        length = dx * dx + dy * dy
        t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / length)) if length else 0
        nearest = min(nearest, (x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2)
        if (ay > y) != (by > y) and x < ax + (y - ay) * dx / dy:
            inside = not inside
        previous = current
    return math.sqrt(nearest) * (-1 if inside else 1)


def floats(values):
    return '{' + ', '.join(f'{v:.5f}f' for v in values) + '}'


def main(out_h):
    definitions = robot_art.expressions()
    lines = [
        '// Generated from original Pocket robot artwork in robot_art.py. Do not edit.',
        '#pragma once',
        '#include <stdint.h>',
        f'static constexpr uint32_t kRobotShell = 0x{robot_art.SHELL:06X};',
        f'static constexpr uint32_t kRobotVisor = 0x{robot_art.VISOR:06X};',
        f'static constexpr uint32_t kRobotEyes = 0x{robot_art.EYES:06X};',
        'static constexpr float kRobotVisorWidth = %.1ff, kRobotVisorHeight = %.1ff, '
        'kRobotVisorRadius = %.1ff, kRobotVisorY = %.1ff;'
        % (
            robot_art.VISOR_WIDTH,
            robot_art.VISOR_HEIGHT,
            robot_art.VISOR_RADIUS,
            robot_art.VISOR_Y,
        ),
        'static constexpr int kHeadCount = 1;',
        f'static constexpr int kSdfN = {N};',
        f'static constexpr float kSdfLo = {LO}f, kSdfStep = {STEP:.8f}f, kSdfQuant = {QUANT}f;',
        'static const int8_t kHeadSdf[kHeadCount][kSdfN * kSdfN] = {{',
        ','.join(
            str(
                quantize(
                    robot_art.shell_distance(
                        LO + x * STEP - robot_art.CENTER, LO + y * STEP - robot_art.CENTER
                    )
                )
            )
            for y in range(N)
            for x in range(N)
        ),
        '}};',
        f'static constexpr int kExprCount = {len(definitions)};',
        f'static constexpr int kEyePts = {robot_art.POINTS};',
        'static const float kExpr[kExprCount][2][5] = {',
    ]
    contours, textures, metadata = [], [], []
    for name, pair in definitions:
        caps, contour_pair, info = [], [], []
        for points in pair:
            x0, y0 = [min(p[axis] for p in points) for axis in (0, 1)]
            x1, y1 = [max(p[axis] for p in points) for axis in (0, 1)]
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            a, b = (x1 - x0) / 2, (y1 - y0) / 2
            caps.append(floats((cx, cy, 0 if b >= a else math.pi / 2, abs(a - b), min(a, b))))
            contour_pair.append('{' + ','.join(floats(p) for p in points) + '}')
            local = [(x - cx, y - cy) for x, y in points]
            ox, oy = math.floor(x0 - cx) - 3, math.floor(y0 - cy) - 3
            w, h = math.ceil(x1 - cx) + 4 - ox, math.ceil(y1 - cy) + 4 - oy
            if w * h > 7680:
                raise ValueError(f'{name}: eye texture exceeds firmware RAM capacity')
            info.append('{' + ','.join(map(str, (len(textures), w, h, ox, oy))) + '}')
            textures.extend(
                quantize(polygon_distance(local, ox + x, oy + y))
                for y in range(h)
                for x in range(w)
            )
        lines.append('{' + ','.join(caps) + '}, // ' + name)
        contours.append('{' + ','.join(contour_pair) + '},')
        metadata.append('{' + ','.join(info) + '},')
    lines += [
        '};',
        'static const float kEyeContour[kExprCount][2][kEyePts][2] = {',
        *contours,
        '};',
        f'static const int8_t kEyeTex[{len(textures)}] = {{',
        ','.join(map(str, textures)),
        '};',
        'static const int32_t kEyeTexInfo[kExprCount][2][5] = {',
        *metadata,
        '};',
    ]
    Path(out_h).write_text('\n'.join(lines) + '\n')
    print('Wrote original Pocket robot:', out_h)


if __name__ == '__main__':
    main(sys.argv[1])
