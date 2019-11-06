#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Node implementation for the ComponentGraph.

The ComponentNode constructed with a package stores rich data about all the
services this component offers, uses and exposes. This is all stored directly
on the node as aggregation data during generation to make the structure as
simple as possible to use without any further aggregation.
"""

from server.util.url import package_resource_url


class ComponentNode:
    """ Node implementation for the component graph. """

    def __init__(self, resource_path, resource_data, ty):
        self.id = resource_path
        self.name = resource_path.split("/")[-1]
        self.manifest = resource_data
        self.exposes = []
        self.offers = []
        self.version = 0
        self.consumers = 0
        self.source = "package"
        self.ty = ty
        # Filled in when links are generated
        self.component_use_deps = []
        try:
            self.uses = self.manifest["sandbox"]["services"]
        except KeyError:
            self.uses = []
        try:
            self.features = self.manifest["sandbox"]["features"]
        except KeyError:
            self.features = []

    @classmethod
    def create_inferred(cls, pkg_resource_url):
        """ Named constructor to construct an inferred ComponentNode. """
        node = cls(pkg_resource_url, {}, "inferred")
        return node

    def append_offer(self, offer):
        """ Adds an additional service this component offers. """
        self.offers.append(offer)

    def __str__(self):
        return self.id

    def export(self):
        """ Returns an export of the node that can be transformed into JSON """
        export_data = {}
        export_data["id"] = self.id
        export_data["name"] = self.name
        export_data["manifest"] = self.manifest
        export_data["consumers"] = self.consumers
        export_data["routes"] = {}
        export_data["version"] = self.version
        export_data["source"] = self.source
        export_data["routes"]["exposes"] = self.exposes
        export_data["routes"]["offers"] = self.offers
        export_data["routes"]["uses"] = self.uses
        return export_data
