#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from server.graph.component_graph_generator import *


class TestComponentGraphGenerator(unittest.TestCase):

    def test_add_node(self):
        package_0 = {
            "url": "fuchsia-pkg://fuchsia.com/AAA",
            "merkle": "FCFCFC",
            "type": "package",
            "files": {
                "meta/contents": "",
                "meta/AAA.cmx": {}
            },
        }
        package_1 = {
            "url": "fuchsia-pkg://fuchsia.com/BBB",
            "merkle": "FEFEFE",
            "type": "package",
            "files":
                {
                    "meta/contents": "",
                    "meta/BBB.cmx":
                        {
                            "sandbox": {
                                "services": ["fuchsia.test.Service"]
                            }
                        }
                },
        }
        service_mappings = {
            "fuchsia.test.Service": "fuchsia-pkg://fuchsia.com/AAA#meta/AAA.cmx"
        }
        generator = ComponentGraphGenerator()
        graph = generator.generate([package_0, package_1], service_mappings)
        self.assertEqual(len(graph.nodes), 2)
        self.assertEqual(len(graph.links), 1)
        self.assertEqual(
            graph.links[0].source, package_1["url"] + "#meta/BBB.cmx")
        self.assertEqual(
            graph.links[0].target, package_0["url"] + "#meta/AAA.cmx")
        self.assertEqual(graph.links[0].route_type, "use")
        self.assertEqual(graph.links[0].fidl_service, "fuchsia.test.Service")


if __name__ == "__main__":
    unittest.main()
