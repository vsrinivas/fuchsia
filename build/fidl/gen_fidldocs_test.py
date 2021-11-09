#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import tempfile
import unittest
from unittest import mock

import gen_fidldocs


class GenReferenceDocsTest(unittest.TestCase):

    def setUp(self):
        # Create a temporary directory
        self.temp_dir = tempfile.mkdtemp()

    def test_read_fidl_docs(self):
        with mock.patch("builtins.open", mock.mock_open(
                read_data='[{"ir":"fakefidl_a"},{"ir":"fakefidl_b"}]')):
            files = gen_fidldocs.read_fidl_packages(self.temp_dir)
            self.assertEquals(files, ["fakefidl_a", "fakefidl_b"])

    def test_run_fidl_doc(self):
        build_dir = os.path.join(self.temp_dir, 'out')
        out_dir = os.path.join(self.temp_dir, 'gen')
        mock_files = ['a', 'b', 'c']
        with mock.patch.object(gen_fidldocs.subprocess, 'run') as mock_run:
            fidldoc_path = os.path.join(build_dir, 'host-tools', 'fidldoc')
            out_fidl = os.path.join(out_dir, 'fidldoc')
            gen_fidldocs.run_fidl_doc(build_dir, out_dir, mock_files)
            mock_run.assert_called_once_with(
                [
                    fidldoc_path, "--verbose", "--path", "/reference/fidl/",
                    "--out", out_fidl
                ] + mock_files)
