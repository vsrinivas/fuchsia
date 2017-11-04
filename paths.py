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
    "Linux": "linux",
    "Darwin": "mac",
    "Windows": "win"
}[platform.system()]

DART_ROOT = os.path.join(FUCHSIA_ROOT, "third_party", "dart", "tools", "sdks",
                         DART_PLATFORM, "dart-sdk")
