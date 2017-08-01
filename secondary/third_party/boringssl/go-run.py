#! /usr/bin/python

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import sys

def main():
    go = os.path.join(sys.argv[3], 'go')
    with open(sys.argv[2], "w") as f:
        return subprocess.call([go, 'run'] + sys.argv[4:], stdout=f, cwd=sys.argv[1])

if __name__ == "__main__":
    sys.exit(main())

