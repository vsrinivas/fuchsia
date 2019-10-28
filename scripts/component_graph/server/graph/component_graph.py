#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""ComponentGraph is a graph that holds component relationships.

Typical example usage:
  graph = ComponentGraph()
  graph.add_node(node)
  graph.add_link(link)
  graph["node_name"].add_uses(...)
  graph.export()
"""


class ComponentGraph:
    """ A simple graph structure capable of exporting to JSON """

    def __init__(self):
        self.nodes = {}
        self.links = []

    def add_node(self, node):
        """ Adds a new ComponentNode to the graph. """
        self.nodes[node.id] = node

    def add_link(self, link):
        """ Adds a new ComponentLink to the graph. """
        self.links.append(link)

    def __getitem__(self, node_id):
        """ Returns the node in the graph """
        return self.nodes[node_id]

    def export(self):
        """ Converts the graph structure into a JSON adjacency matrix. """
        export_data = {"nodes": [], "links": []}
        for _, node in self.nodes.items():
            export_data["nodes"].append(node.export())
        for link in self.links:
            export_data["links"].append(link.export())
        return export_data
