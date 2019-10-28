#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from server.graph.component_link import *


class TestComponentLink(unittest.TestCase):

    def test_export(self):
        component_link = ComponentLink("A", "B", "uses", "fuchsia.service.Test")
        export = component_link.export()
        self.assertEqual(export["source"], "A")
        self.assertEqual(export["target"], "B")
        self.assertEqual(export["type"], "uses")
        self.assertEqual(export["fidl_service"], "fuchsia.service.Test")


if __name__ == "__main__":
    unittest.main()
