# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os.path
import platform

FUCHSIA_DIR = os.environ["FUCHSIA_DIR"]

PREBUILT_DIR = os.path.join(FUCHSIA_DIR, "prebuilt")
FUCHSIA_BUILD_DIR = os.environ["FUCHSIA_BUILD_DIR"]

PREBUILT_PLATFORM = {
    "Linux": "linux-x64",
    "Darwin": "mac-x64",
    "Windows": "win-x64"
}[platform.system()]

FLUTTER_ROOT = os.path.join(
    FUCHSIA_DIR, "third_party", "dart-pkg", "git", "flutter")
DART_ROOT = os.path.join(PREBUILT_DIR, "third_party", "dart", PREBUILT_PLATFORM)
