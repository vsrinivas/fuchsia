#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

if __name__ == '__main__':
    test_dir = os.path.dirname(os.path.abspath(__file__))
    tests = unittest.defaultTestLoader.discover(test_dir, pattern=sys.argv[1])
    unittest.runner.TextTestRunner(verbosity=1).run(tests)
