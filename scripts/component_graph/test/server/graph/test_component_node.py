#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from server.graph.component_graph import *
from server.graph.component_link import *
from server.graph.component_node import *


class TestComponentNode(unittest.TestCase):

    def test_add_node(self):
        manifest = {}
        resource_path = "fuchsia-pkg//fuchsia.com/test#meta/test.cmx"
        node = ComponentNode(resource_path, manifest)
        self.assertEqual(node.source, "package")
        self.assertEqual(node.version, 0)
        self.assertEqual(node.offers, [])
        self.assertEqual(node.uses, [])
        self.assertEqual(node.exposes, [])

    def test_create_inferred(self):
        pkg_url = "fuchsia-pkg://test/test.cmx"
        inferred_node = ComponentNode.create_inferred(pkg_url)
        self.assertIsNotNone(inferred_node)

    def test_append_offer(self):
        manifest = {}
        resource_path = "fuchsia-pkg//fuchsia.com/test#meta/test.cmx"
        node = ComponentNode(resource_path, manifest)
        self.assertEqual(node.offers, [])
        node.append_offer("fuchsia.service.Test")
        self.assertEqual(node.offers, ["fuchsia.service.Test"])

    def test__str__(self):
        manifest = {}
        resource_path = "fuchsia-pkg//fuchsia.com/test#meta/test.cmx"
        node = ComponentNode(resource_path, manifest)
        self.assertEqual(resource_path, str(node))

    def test_export(self):
        manifest = {}
        resource_path = "fuchsia-pkg//fuchsia.com/test#meta/test.cmx"
        node = ComponentNode(resource_path, manifest)
        node.append_offer("fuchsia.service.Test")
        export_data = node.export()
        self.assertEqual(export_data["id"], resource_path)
        self.assertEqual(export_data["name"], "test.cmx")
        self.assertEqual(export_data["consumers"], 0)
        self.assertEqual(
            export_data["routes"], {
                "exposes": [],
                "offers": ["fuchsia.service.Test"],
                "uses": []
            })
        self.assertEqual(export_data["version"], 0)
        self.assertEqual(export_data["source"], "package")
        self.assertEqual(export_data["routes"]["exposes"], [])
        self.assertEqual(
            export_data["routes"]["offers"], ["fuchsia.service.Test"])
        self.assertEqual(export_data["routes"]["uses"], [])


if __name__ == "__main__":
    unittest.main()
