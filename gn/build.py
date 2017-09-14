#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser(description="Build Fuchsia")
    parser.add_argument("--release", "-r", help="build in release mode",
            action="store_true")
    args, unknown_args = parser.parse_known_args()
    zircon_build_script = os.path.join(paths.FUCHSIA_ROOT, "scripts", "build-zircon.sh")
    subprocess.check_call([zircon_build_script])
    ninja_path = os.path.join(paths.FUCHSIA_ROOT, "buildtools", "ninja")
    outdir = paths.DEBUG_OUT_DIR
    if args.release:
        outdir = paths.RELEASE_OUT_DIR
    return subprocess.call([ninja_path, "-C", outdir] + unknown_args)

if __name__ == "__main__":
    sys.exit(main())
