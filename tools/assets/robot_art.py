"""Original Pocket robot artwork, authored without downloaded website geometry.

Coordinates use a 256-unit canvas. Expression numbers are stable diagnostics.
"""

import math

CENTER = 128.0
POINTS = 64
SHELL = 0xFFD38A
VISOR = 0x1A2938
EYES = 0xA9FFE3
SHELL_HALF_WIDTH, SHELL_HALF_HEIGHT, SHELL_RADIUS = 96, 85, 43
FRAME_INSET = 16
# Inset the same rounded rectangle on every side, including its corner radius.
VISOR_WIDTH = SHELL_HALF_WIDTH - FRAME_INSET
VISOR_HEIGHT = SHELL_HALF_HEIGHT - FRAME_INSET
VISOR_RADIUS = SHELL_RADIUS - FRAME_INSET
VISOR_Y = 0


def rounded_box(x, y, half_x, half_y, radius):
    dx, dy = abs(x) - half_x + radius, abs(y) - half_y + radius
    return math.hypot(max(dx, 0), max(dy, 0)) + min(max(dx, dy), 0) - radius


def shell_distance(x, y):
    # The short leaning antenna and pill-shaped ears soften the silhouette.
    ax, ay, dx, dy = x + 8, y + 81, -5, -20
    t = max(0, min(1, (ax * dx + ay * dy) / (dx * dx + dy * dy)))
    return min(
        rounded_box(x, y, SHELL_HALF_WIDTH, SHELL_HALF_HEIGHT, SHELL_RADIUS),
        rounded_box(abs(x) - 101, y + 1, 11, 18, 10),
        math.hypot(ax - t * dx, ay - t * dy) - 3.5,
        math.hypot(x + 13, y + 102) - 10.5,
    )


def visor_distance(x, y):
    return rounded_box(x, y - VISOR_Y, VISOR_WIDTH, VISOR_HEIGHT, VISOR_RADIUS)


def resample(points):
    edges = list(zip(points, points[1:] + points[:1]))
    lengths = [math.dist(a, b) for a, b in edges]
    result, edge, passed = [], 0, 0.0
    for i in range(POINTS):
        at = sum(lengths) * i / POINTS
        while edge < len(edges) - 1 and passed + lengths[edge] < at:
            passed += lengths[edge]
            edge += 1
        a, b = edges[edge]
        t = (at - passed) / lengths[edge]
        result.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return result


def eye(width=16, height=24, tilt=0, arch=0, diamond=False, offset=0):
    if arch:
        points = []
        for side in (-1, 1):
            for j in range(32) if side < 0 else range(31, -1, -1):
                x = width * (-1 + 2 * j / 31)
                points.append((x, -arch * (1 - (x / width) ** 2) + side * height))
    elif diamond:
        points = [(0, -height), (width, 0), (0, height), (-width, 0)]
    else:
        points = []
        for i in range(POINTS):
            t = math.tau * i / POINTS
            x = width * math.copysign(abs(math.cos(t)) ** 0.9, math.cos(t))
            y = height * math.copysign(abs(math.sin(t)) ** 0.9, math.sin(t))
            points.append((x, y))
    cs, sn = math.cos(tilt), math.sin(tilt)
    return resample([(x * cs - y * sn, x * sn + y * cs + offset) for x, y in points])


def expressions():
    """25 eye pairs with semantic names for inspection and preview."""
    definitions = [
        ('Ready', {}, {}),
        ('Gentle', dict(width=16, height=19), dict(width=16, height=19)),
        ('Joy', dict(width=18, height=4, arch=11), dict(width=18, height=4, arch=11)),
        ('Surprise', dict(width=19, height=25), dict(width=19, height=25)),
        ('Tired', dict(width=16, height=8, tilt=0.12), dict(width=16, height=8, tilt=-0.12)),
        ('Focus', dict(width=12, height=18, tilt=-0.22), dict(width=12, height=18, tilt=0.22)),
        ('Left wink', dict(width=16, height=3, arch=5), {}),
        ('Busy', dict(width=10, height=17), dict(width=10, height=17)),
        ('Attentive', dict(width=17, height=26), dict(width=17, height=26)),
        ('Right wink', {}, dict(width=16, height=3, arch=5)),
        ('Determined', dict(width=17, height=9, tilt=0.25), dict(width=17, height=9, tilt=-0.25)),
        ('Grin', dict(width=18, height=5, arch=14), dict(width=18, height=5, arch=14)),
        ('Skeptical', dict(width=15, height=7, tilt=-0.2), dict(width=12, height=20)),
        ('Asleep', dict(width=16, height=2.5, arch=-4), dict(width=16, height=2.5, arch=-4)),
        ('Ponder', dict(width=12, height=18, offset=-5), dict(width=14, height=12, offset=-5)),
        ('Curious', dict(width=19, height=27), dict(width=14, height=20)),
        ('Concentrate', dict(width=16, height=8), dict(width=16, height=8)),
        (
            'Sparkle',
            dict(width=17, height=24, diamond=True),
            dict(width=17, height=24, diamond=True),
        ),
        ('Puzzled', dict(width=12, height=22, tilt=-0.3), dict(width=12, height=15, tilt=0.3)),
        ('Content', dict(width=16, height=3, arch=8), dict(width=16, height=3, arch=8)),
        ('Shy', dict(width=13, height=17, offset=4), dict(width=13, height=17, offset=4)),
        ('Amazed', dict(width=21, height=27), dict(width=21, height=27)),
        ('Drowsy', dict(width=17, height=4, tilt=0.08), dict(width=17, height=4, tilt=-0.08)),
        ('Worried', dict(width=15, height=10, tilt=-0.3), dict(width=15, height=10, tilt=0.3)),
        ('Sad', dict(width=16, height=3.5, arch=-10), dict(width=16, height=3.5, arch=-10)),
    ]
    return [
        (
            name,
            [
                [(x + CENTER + side * 31, y + CENTER - 9) for x, y in eye(**params)]
                for side, params in ((-1, left), (1, right))
            ],
        )
        for name, left, right in definitions
    ]
