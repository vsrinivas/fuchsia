#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from server.graph.component_graph import *
from server.graph.component_link import *
from server.graph.component_node import *


class TestComponentGraph(unittest.TestCase):

    def test_add_node(self):
        component_graph = ComponentGraph()
        self.assertEqual(len(component_graph.nodes), 0)
        component_graph.add_node(
            ComponentNode(
                "fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx", {}))
        self.assertEqual(len(component_graph.nodes), 1)
        component_graph.add_node(
            ComponentNode(
                "fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx", {}))
        self.assertEqual(len(component_graph.nodes), 2)

    def test_add_link(self):
        component_graph = ComponentGraph()
        component_graph.add_node(
            ComponentNode(
                "fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx", {}))
        component_graph.add_node(
            ComponentNode(
                "fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx", {}))
        self.assertEqual(len(component_graph.links), 0)
        component_graph.add_link(
            ComponentLink(
                "fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx",
                "fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx", "use",
                "fidl.Service"))
        self.assertEqual(len(component_graph.links), 1)
        component_graph.add_link(
            ComponentLink(
                "fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx",
                "fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx", "use",
                "fidl.AnotherService"))
        self.assertEqual(len(component_graph.links), 2)

    def test_get_item(self):
        component_graph = ComponentGraph()
        component_graph.add_node(
            ComponentNode(
                "fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx", {}))
        component_graph.add_node(
            ComponentNode(
                "fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx", {}))
        self.assertIsNotNone(
            component_graph["fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx"]
        )
        self.assertIsNotNone(
            component_graph["fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx"]
        )
        with self.assertRaises(KeyError):
            self.assertIsNotNone(component_graph["fake_url3"])

    def test_export_empty(self):
        component_graph = ComponentGraph()
        empty_export = component_graph.export()
        self.assertEqual(empty_export["nodes"], [])
        self.assertEqual(empty_export["links"], [])

    def test_export(self):
        component_graph = ComponentGraph()
        component_graph.add_node(
            ComponentNode(
                "fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx", {}))
        component_graph.add_node(
            ComponentNode(
                "fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx", {}))
        self.assertEqual(len(component_graph.links), 0)
        component_graph.add_link(
            ComponentLink(
                "fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx",
                "fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx", "use",
                "fidl.Service"))
        self.assertEqual(len(component_graph.links), 1)
        component_graph.add_link(
            ComponentLink(
                "fuchsia-pkg://fuchsia.com/test_2#/meta/test_2.cmx",
                "fuchsia-pkg://fuchsia.com/test_1#/meta/test_1.cmx", "use",
                "fidl.AnotherService"))
        self.assertEqual(len(component_graph.links), 2)
        exported = component_graph.export()
        self.assertEqual(len(exported["nodes"]), 2)
        self.assertEqual(len(exported["links"]), 2)


if __name__ == "__main__":
    unittest.main()
