#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest import mock
import urllib.request
from urllib.error import HTTPError, URLError
from server.fpm.package_manager import *


class TestPackageManager(unittest.TestCase):

    def create_package_manager(self):
        """ Returns a package manager populated with fake data. """
        return PackageManager('http://localhost:8083', '/home/test/fuchsia')

    def mock_urlopen_response(self, mock_urlopen, code=200, data='{}'):
        mock_response = mock.MagicMock()
        mock_response.read.return_value = data
        mock_response.getcode.return_value = code
        if code != 200:
            mock_urlopen.side_effect = URLError(str(code))
        mock_response.__enter__.return_value = mock_response
        mock_urlopen.return_value = mock_response
        return mock_urlopen

    @mock.patch.object(urllib.request, 'urlopen', autospec=True)
    def test_ping(self, mock_urlopen):
        mock_urlopen = self.mock_urlopen_response(mock_urlopen)
        package_manager = self.create_package_manager()
        self.assertTrue(package_manager.ping())
        mock_urlopen.assert_called()

    @mock.patch.object(urllib.request, 'urlopen', autospec=True)
    def test_ping_fail(self, mock_urlopen):
        mock_urlopen = self.mock_urlopen_response(mock_urlopen, 404)
        package_manager = self.create_package_manager()
        self.assertFalse(package_manager.ping())
        mock_urlopen.assert_called()

    @mock.patch.object(urllib.request, 'urlopen', autospec=True)
    def test_get_blob(self, mock_urlopen):
        package_manager = self.create_package_manager()
        mock_urlopen = self.mock_urlopen_response(
            mock_urlopen, 200, b'blob_data')
        package_manager = self.create_package_manager()
        self.assertEqual(package_manager.get_blob('merkle'), b'blob_data')
        mock_urlopen.assert_called()

    @mock.patch.object(urllib.request, 'urlopen', autospec=True)
    def test_get_blob_fail(self, mock_urlopen):
        package_manager = self.create_package_manager()
        mock_urlopen = self.mock_urlopen_response(mock_urlopen, 404)
        package_manager = self.create_package_manager()
        self.assertIsNone(package_manager.get_blob('merkle'))
        mock_urlopen.assert_called()

    @mock.patch.object(urllib.request, 'urlopen', autospec=True)
    def test_get_packages(self, mock_urlopen):
        package_manager = self.create_package_manager()
        data = b"""
    {
      "signed": {
        "targets": {
          "test_comp/0": {"custom": {"merkle": "AAAA"}},
          "test_comp2/0": {"custom": {"merkle": "BBBB"}}
        }
      }
    }
    """
        mock_urlopen = self.mock_urlopen_response(mock_urlopen, 200, data)
        packages = package_manager.get_packages()
        self.assertEqual(
            packages[0]['url'], 'fuchsia-pkg://fuchsia.com/test_comp')
        self.assertEqual(packages[0]['merkle'], 'AAAA')
        self.assertEqual(
            packages[1]['url'], 'fuchsia-pkg://fuchsia.com/test_comp2')
        self.assertEqual(packages[1]['merkle'], 'BBBB')


if __name__ == '__main__':
    unittest.main()
