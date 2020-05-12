"""
Constants that come from the environment. These are in a separate module so
that they can be mocked by tests if necessary.
"""
import os
from pathlib import Path
import sys

FUCHSIA_DIR = Path(os.environ["FUCHSIA_DIR"])
assert FUCHSIA_DIR.exists()

with open(FUCHSIA_DIR / ".fx-build-dir") as f:
    BUILD_DIR = f.read().strip()

if sys.platform.startswith('linux'):
    PLATFORM = 'linux'
elif sys.platform == 'darwin':
    PLATFORM = 'mac'
else:
    print("Unsupported platform: " + sys.platform)
    sys.exit(1)

with open(FUCHSIA_DIR / (BUILD_DIR + '.zircon') / 'args.gn') as f:
    MODE = 'asan' if 'asan' in f.read() else 'clang'
