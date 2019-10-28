#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from server.util.logging import *


class TestLogging(unittest.TestCase):

    def test_logger_not_none(self):
        self.assertNotEqual(get_logger(__name__), None)


if __name__ == '__main__':
    unittest.main()
