#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Serves JSON API requests.

The ApiHandler is an bridge layer between the network and the query module. It
takes query requests through fixed GET requests and returns JSON representations
of the query.
"""

import json
from server.util.logging import get_logger
from server.query import QueryHandler


def json_pretty_print(python_object):
    """ Creates well formatted json from a python object """
    return json.dumps(
        python_object, sort_keys=True, indent=2, separators=(",", ":"))


class ApiHandler:
    """ Handles all of the JSON API requests. """

    def __init__(self, package_manager):
        """ Initializes the query handler with the package manager object """
        self.logger = get_logger(__name__)
        self.query_handler = QueryHandler(package_manager)

    def respond(self, path):
        """ Forwards API calls to their respective handlers. """
        if path == "/api/component/packages":
            return json_pretty_print(self.query_handler.packages())
        if path == "/api/component/services":
          return json_pretty_print(self.query_handler.services(self.query_handler.packages()))
        if path == "/api/component/graph":
            return json_pretty_print(self.query_handler.component_graph())
        return None
