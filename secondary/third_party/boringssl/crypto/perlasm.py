#! /usr/bin/python

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import subprocess
import sys

def main():
    return subprocess.call(sys.argv[1:])

if __name__ == "__main__":
    sys.exit(main())
