#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generates ComponentGraphs from PackageManager data.

The ComponentGraphGenerator is responsible for taking raw Package and service
configuration data and converting this into a ComponentGraph. It handles the
core business logic in the application.
"""
from server.graph import ComponentGraph
from server.graph.component_link import ComponentLink
from server.graph.component_node import ComponentNode
from server.util.url import package_resource_url
from server.util.logging import get_logger


def create_inferred_node(pkg_url, fidl_service):
    """ Returns an inferred node derived from a missing link target. """
    inferred_node = ComponentNode.create_inferred(pkg_url)
    inferred_node.append_offer(fidl_service)
    return inferred_node


def component_url_strip_arguments(pkg_url):
    """ Strips optional arguments provided in package urls defined in service mappings. """
    node_id = pkg_url
    # This deals with one specific case of a package url using a list.
    if isinstance(node_id, list):
        node_id = node_id[0]
    return node_id


class ComponentGraphGenerator:
    """Responsible for generating the component graph from the provided sources"""

    def __init__(self):
        """ Sets up the logging system for the generator. """
        self.logger = get_logger(__name__)

    def generate(self, packages, service_mappings):
        """ Generates the nodes and links of the graph by analyzing the packages """
        graph = ComponentGraph()
        # Pass 1: Add all the components to the graph
        for package in packages:
            for path, data in package["files"].items():
                # TODO(benwright) - Add cm when V2 is supported.
                if path.startswith("meta/") and path.endswith(".cmx"):
                    node = ComponentNode(
                        package_resource_url(package["url"], path), data)
                    graph.add_node(node)

        # Pass 2: Resolve all the fidl service mappings and attach them to their
        # nodes.
        for fidl_service, pkg_url in service_mappings.items():
            node_id = component_url_strip_arguments(pkg_url)
            try:
                graph[node_id].append_offer(fidl_service)
            except KeyError:
                self.logger.warning(
                    "Missing service package: %s adding inferred node", pkg_url)
                graph.add_node(create_inferred_node(pkg_url, fidl_service))

        # Pass 3: Connect all the links.
        graph_nodes = list(graph.nodes.values())
        for node in graph_nodes:
            for use_route in node.uses:
                if use_route in service_mappings:
                    service_url = component_url_strip_arguments(service_mappings[use_route])
                    link = ComponentLink(node.id, service_url, "use", use_route)
                    graph[service_url].consumers += 1
                    graph.add_link(link)
                else:
                    self.logger.warning(
                        "Missing use route from %s to %s adding inferred node",
                        node.id, use_route)
                    inferred_pkg_url = "fuchsia-pkg://inferred#meta/" + use_route + ".cmx"
                    graph.add_node(
                        create_inferred_node(inferred_pkg_url, use_route))
                    service_mappings[use_route] = inferred_pkg_url
                    link = ComponentLink(
                        node.id, inferred_pkg_url, "use", use_route)
                    graph[inferred_pkg_url].consumers += 1
                    graph.add_link(link)

        return graph
