#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
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
        dcmp = filecmp.dircmp(outdir, os.path.join(SCRIPT_DIR, 'golden'))
        self.verify_contents_recursive(dcmp)

    def verify_contents_recursive(self, dcmp):
        """Recursively checks for differences between two directories.

        Fails if the directories do not appear to be deeply identical in
        structure and content.

        Args:
            dcmp (filecmp.dircmp): A dircmp of the directories.
        """
        if dcmp.left_only or dcmp.right_only or dcmp.diff_files:
            self.fail(f"Generated SDK does not match golden files.\n"
                f"Only in {dcmp.left}:\n{dcmp.left_only}\n\n"
                f"Only in {dcmp.right}:\n{dcmp.right_only}\n\n"
                f"Common different files:\n{dcmp.diff_files}")
        for sub_dcmp in dcmp.subdirs.values():
            self.verify_contents_recursive(sub_dcmp)

def TestMain():
    unittest.main()


if __name__ == '__main__':
    TestMain()
