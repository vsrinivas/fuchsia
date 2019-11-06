#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""QueryHandler services all graph and package queries.

The QueryHandler is the business logic layer between the PackageManager and
the ComponentGraphGenerator. It is responsible for processing the data provided
by the PackageManager and forwarding it through for graph generation.
"""

import sys
import json
from server.util.logging import get_logger
from server.graph import ComponentGraphGenerator

class ComponentQueryError(Exception):
  """Raised when an unrecoverable query exception occurs"""
  pass

class QueryHandler:
    """ Core handler to respond to different queries """

    def __init__(self, package_manager):
        """ Verifies the package manager is online. """
        self.logger = get_logger(__name__)
        self.package_manager = package_manager
        self.graph_generator = ComponentGraphGenerator()

        if not self.package_manager.ping():
            self.logger.error(
                "Failed to connect to package manager please run fx serve.")
            sys.exit(1)

    def services(self, packages):
        """ Returns the list of service to component url mappings """
        return self.package_manager.get_services(packages)

    def packages(self):
        """ Returns a list of available packages """
        return self.package_manager.get_packages()

    def component_graph(self):
        """ Returns the component graph that shows all component connections """
        packages = self.packages()
        return self.graph_generator.generate(packages, self.services(packages)).export()
