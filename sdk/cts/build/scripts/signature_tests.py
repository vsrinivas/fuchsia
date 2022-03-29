#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import sys

import plasa_differ


def main():
    try:
        plasa_differ.main()
    except Exception as e:
        print(e)
        # TODO(fxbug.dev/92170): Make this a blocking test once the FIDL
        # versioning strategy has been implemented.
        pass


# The python_host_test build rule calls `unittest.main`.
# Since this is not a unittest (and can't be since it takes command line args)
# we pretend our main is unittest's main.
unittest = sys.modules[__name__]

if __name__ == '__main__':
    unittest.main()
