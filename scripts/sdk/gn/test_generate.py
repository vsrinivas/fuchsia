#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import generate
import os
import shutil
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

TMP_DIR_NAME = tempfile.mkdtemp(prefix='tmp_unittest_%s_' % 'GNGenerateTest')


class GNGenerateTest(unittest.TestCase):

    def setUp(self):
        # make sure TMP_DIR_NAME is empty
        if os.path.exists(TMP_DIR_NAME):
            shutil.rmtree(TMP_DIR_NAME)
        os.makedirs(TMP_DIR_NAME)

    def tearDown(self):
        if os.path.exists(TMP_DIR_NAME):
            shutil.rmtree(TMP_DIR_NAME)

    # Use a mock to patch in the command line arguments.
    @mock.patch(
        'argparse.ArgumentParser.parse_args',
        return_value=argparse.Namespace(
            output=TMP_DIR_NAME,
            archive='',
            directory=os.path.join(SCRIPT_DIR, 'testdata')))
    def testEmptyArchive(self, mock_args):
        # Run the generator.
        generate.main()
        self.verify_contents(TMP_DIR_NAME)

    def verify_contents(self, outdir):
        self.assertTrue(os.path.exists(os.path.join(outdir, 'README.md')))
        self.assertTrue(
            os.path.exists(os.path.join(outdir, 'meta', 'manifest.json')))


def TestMain():
    unittest.main()


if __name__ == '__main__':
    TestMain()
