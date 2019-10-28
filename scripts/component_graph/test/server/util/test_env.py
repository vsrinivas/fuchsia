#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest import mock
from server.util.env import *


class TestEnv(unittest.TestCase):

    @mock.patch('os.path.exists')
    def test_fuchsia_root_no_path_is_null(self, os_path_exists_mock):
        os_path_exists_mock.return_value = False
        with mock.patch.dict('os.environ',
                             {'FUCHSIA_DIR': '/home/test/fuchsia'}, clear=True):
            self.assertEqual(get_fuchsia_root(), None)

    @mock.patch('os.path.exists')
    def test_fuchsia_root_no_slash(self, os_path_exists_mock):
        os_path_exists_mock.return_value = True
        with mock.patch.dict('os.environ',
                             {'FUCHSIA_DIR': '/home/test/fuchsia'}, clear=True):
            self.assertEqual(get_fuchsia_root(), '/home/test/fuchsia/')

    @mock.patch('os.path.exists')
    def test_fuchsia_root_with_slash(self, os_path_exists_mock):
        os_path_exists_mock.return_value = True
        with mock.patch.dict('os.environ',
                             {'FUCHSIA_DIR': '/home/test/fuchsia/'},
                             clear=True):
            self.assertEqual(get_fuchsia_root(), '/home/test/fuchsia/')


if __name__ == '__main__':
    unittest.main()
