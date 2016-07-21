#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import sys
import paths

def run(args):
    dotfile_path = os.path.join(paths.SCRIPT_DIR, ".gn")
    gn_args = [
        paths.GN_PATH,
        "--root=%s" % paths.FUCHSIA_ROOT,
        "--dotfile=%s" % dotfile_path,
        "--script-executable=/usr/bin/env"
        ] + args
    return subprocess.call(gn_args)

if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
