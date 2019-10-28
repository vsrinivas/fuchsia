#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest import mock
from server.net.api_handler import *
from server.query import QueryHandler


class TestApiHandler(unittest.TestCase):

    @mock.patch.object(QueryHandler, "packages", autospec=True)
    def test_respond_packages(self, mock_packages_method):
        api_handler = ApiHandler(mock.Mock())
        mock_packages_method.return_value = {}
        self.assertIsNotNone(api_handler.respond("/api/component/packages"))
        mock_packages_method.assert_called()

    @mock.patch.object(QueryHandler, "services", autospec=True)
    def test_respond_services(self, mock_services_method):
        api_handler = ApiHandler(mock.Mock())
        mock_services_method.return_value = {}
        self.assertIsNotNone(api_handler.respond("/api/component/services"))
        mock_services_method.assert_called()

    @mock.patch.object(QueryHandler, "component_graph", autospec=True)
    def test_respond_component_graph(self, mock_component_graph_method):
        api_handler = ApiHandler(mock.Mock())
        mock_component_graph_method.return_value = {}
        self.assertIsNotNone(api_handler.respond("/api/component/graph"))
        mock_component_graph_method.assert_called()

    def test_invalid_path(self):
        api_handler = ApiHandler(mock.Mock())
        self.assertIsNone(api_handler.respond(""))
        self.assertIsNone(api_handler.respond("/api"))
        self.assertIsNone(api_handler.respond("/api/"))
        self.assertIsNone(api_handler.respond("/api/component"))
        self.assertIsNone(api_handler.respond("/api/component/g"))
        self.assertIsNone(api_handler.respond("/api/component/graph1"))
        self.assertIsNone(api_handler.respond("/api/component/invalid_path"))


if __name__ == "__main__":
    unittest.main()
