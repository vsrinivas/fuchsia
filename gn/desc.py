#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import gn
import sys
import os
import paths


def main(args):
    return gn.run(["desc", paths.DEBUG_OUT_DIR] + args)

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
