#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
List build landmines. Every line should include a reference to a bug which justifies the addition.

WARNING: Exercise great caution when adding to this file, as the consequences are
significant and widespread. Every Fuchsia developer and builder will have their incremental
build cache invalidated when receiving or reverting the change to do so. Only add to this
file after consulting with the Build team about failed attempts to address build convergence
issues within the dependency graph.
"""

import sys


def print_landmines():
    """
    All landmines are emitted from here.
    """
    pass


def main():
    print_landmines()
    return 0


if __name__ == '__main__':
    sys.exit(main())
