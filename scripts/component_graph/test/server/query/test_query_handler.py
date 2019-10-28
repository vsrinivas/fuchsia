#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest import mock
from server.query.query_handler import *
from server.fpm.package_manager import *


class TestQueryHandler(unittest.TestCase):

    @mock.patch("server.fpm.package_manager.PackageManager")
    def test_services(self, mock_package_manager):
        fake_packages = [
            {
                "url": "fuchsia-pkg://fuchsia.com/test",
                "merkle": "AAA",
                "type": "package",
                "files": {
                    "meta/contents": ""
                },
            },
            {
                "url": "fuchsia-pkg://fuchsia.com/test2",
                "merkle": "BBB",
                "type": "package",
                "files": {
                    "meta/contents": ""
                },
            },
        ]
        fake_contents = [
            (
                "services.config", '{"services": {"fuchsia.test.Test": '
                '"fuchsia-pkg://fuchsia.com/test#meta/test.cmx"}}'),
            (
                "other.config", '{"services": {"fuchsia.test.Test2": '
                '"fuchsia-pkg://fuchsia.com/test2#meta/test2.cmx"}}'),
        ]
        mock_package_manager = mock.Mock()
        mock_package_manager.get_matching_package_contents.return_value = fake_contents
        mock_package_manager.get_packages.return_value = fake_packages
        mock_package_manager.get_builtin_data.return_value = "{}"
        query_handler = QueryHandler(mock_package_manager)
        services = query_handler.services(query_handler.packages())
        self.assertEqual(len(services), 2)

    def test_packages(self):
        fake_packages = [
            {
                "url": "fuchsia-pkg://fuchsia.com/test",
                "merkle": "AAA",
                "type": "package",
                "files": {},
            },
        ]
        mock_package_manager = mock.Mock()
        mock_package_manager.get_packages.return_value = fake_packages
        query_handler = QueryHandler(mock_package_manager)
        self.assertEqual(query_handler.packages(), fake_packages)

    @mock.patch("server.fpm.package_manager.PackageManager")
    def test_component_graph(self, mock_package_manager):
        fake_packages = [
            {
                "url": "fuchsia-pkg://fuchsia.com/test",
                "merkle": "AAA",
                "type": "package",
                "files":
                    {
                        "meta/contents": "",
                        "meta/test.cmx":
                            {
                                "sandbox": {
                                    "services": ["fuchsia.test.Test2"]
                                }
                            }
                    }
            },
            {
                "url": "fuchsia-pkg://fuchsia.com/test2",
                "merkle": "BBB",
                "type": "package",
                "files": {
                    "meta/contents": "",
                    "meta/test2.cmx": {}
                },
            },
        ]
        fake_contents = [
            (
                "services.config", '{"services": {"fuchsia.test.Test": '
                '"fuchsia-pkg://fuchsia.com/test#meta/test.cmx"}}'),
            (
                "other.config", '{"services": {"fuchsia.test.Test2": '
                '"fuchsia-pkg://fuchsia.com/test2#meta/test2.cmx"}}'),
        ]
        mock_package_manager = mock.Mock()
        mock_package_manager.get_matching_package_contents.return_value = fake_contents
        mock_package_manager.get_packages.return_value = fake_packages
        mock_package_manager.get_builtin_data.return_value = "{}"
        query_handler = QueryHandler(mock_package_manager)
        graph = query_handler.component_graph()
        self.assertEqual(len(graph["nodes"]), 2)
        self.assertEqual(len(graph["links"]), 1)


if __name__ == "__main__":
    unittest.main()
