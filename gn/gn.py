#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import sys
import paths


DOTFILE_PATH = os.path.join(paths.SCRIPT_DIR, ".gn")
GN_ARGS = [
    paths.GN_PATH,
    "--root=%s" % paths.FUCHSIA_ROOT,
    "--dotfile=%s" % DOTFILE_PATH,
    "--script-executable=/usr/bin/env"
]


def run(args):
    return subprocess.call(GN_ARGS + args)

if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
