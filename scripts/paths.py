# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os.path
import platform

FUCHSIA_DIR = os.environ["FUCHSIA_DIR"]
ZIRCON_ROOT = os.path.join(FUCHSIA_DIR, "zircon")

PREBUILT_DIR = os.path.join(FUCHSIA_DIR, "prebuilt")
FUCHSIA_BUILD_DIR = os.environ["FUCHSIA_BUILD_DIR"]

# This variable is set by fx, so it is only set if we are running this script
# within fx.
if "ZIRCON_TOOLS_DIR" in os.environ:
    ZIRCON_TOOLS_ROOT = os.path.dirname(os.environ['ZIRCON_TOOLS_DIR'])

PREBUILT_PLATFORM = {
    "Linux": "linux-x64",
    "Darwin": "mac-x64",
    "Windows": "win-x64"
}[platform.system()]

FLUTTER_ROOT = os.path.join(FUCHSIA_DIR, "third_party", "dart-pkg", "git", "flutter")
DART_ROOT = os.path.join(PREBUILT_DIR, "third_party", "dart", PREBUILT_PLATFORM)
