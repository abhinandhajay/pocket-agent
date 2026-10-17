"""Small shared helpers for isolated host fixtures."""

import importlib.util
import time
import sys


class Checks:
    def __init__(self):
        self.count = 0

    def __call__(self, condition, label):
        assert condition, label
        self.count += 1


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    # Match direct script execution when loading a candidate with sibling imports.
    sys.path.insert(0, str(path.resolve().parent))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


def wait(predicate, timeout=3, label='fixture timed out'):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError(label)
