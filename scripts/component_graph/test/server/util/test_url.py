#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from server.util.url import *


class TestUrl(unittest.TestCase):

    def test_package_to_url(self):
        self.assertEqual(
            package_to_url("test"), "fuchsia-pkg://fuchsia.com/test")

    def test_package_resource_url(self):
        self.assertEqual(
            package_resource_url(
                "fuchsia-pkg://fuchsia.com/test", "meta/foo.cmx"),
            "fuchsia-pkg://fuchsia.com/test#meta/foo.cmx")

    def test_strip_package_version(self):
        self.assertEqual(strip_package_version("package/0"), "package")


if __name__ == "__main__":
    unittest.main()
