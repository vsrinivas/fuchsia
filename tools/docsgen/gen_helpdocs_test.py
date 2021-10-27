#!/usr/bin/env fuchsia-vendored-python
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import tempfile
import unittest
from unittest import mock

import gen_helpdocs


class GenReferenceDocsTest(unittest.TestCase):

    def setUp(self):
        # Create a temporary directory
        self.temp_dir = tempfile.mkdtemp()

    def test_run_helpdocs(self):
        out_path = os.path.join(self.temp_dir, 'gen', 'test.tar.gz')
        src_dir = os.path.join(self.temp_dir, 'scripts')

        fx_bin = os.path.join(src_dir, "scripts/fx")
        with mock.patch.object(gen_helpdocs.subprocess, 'run') as mock_run:
            gen_helpdocs.run_fx_helpdoc(src_dir, out_path)
            mock_run.assert_called_once_with(
                [fx_bin, "helpdoc", "--archive", out_path])
