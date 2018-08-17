# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os.path
import platform

FUCHSIA_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZIRCON_ROOT = os.path.join(FUCHSIA_ROOT, "zircon")
BUILDTOOLS_ROOT = os.path.join(FUCHSIA_ROOT, "buildtools")
FLUTTER_ROOT = os.path.join(FUCHSIA_ROOT, "lib", "flutter")

DART_PLATFORM = {
    "Linux": "linux-x64",
    "Darwin": "mac-x64",
    "Windows": "win-x64"
}[platform.system()]

DART_ROOT = os.path.join(FUCHSIA_ROOT, "topaz", "tools", "prebuilt-dart-sdk",
                         DART_PLATFORM)
