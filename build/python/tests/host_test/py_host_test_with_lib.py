#!/usr/bin/env python3

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import lib
import unittest


class PyHostTestWithLibTests(unittest.TestCase):

    def test_truthy(self):
        self.assertEqual(lib.truthy(), True)

    def test_falsy(self):
        self.assertEqual(lib.falsy(), False)


if __name__ == '__main__':
    unittest.main()
