#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import filecmp
import imp
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GENERATE_FILEPATH = os.path.join(SCRIPT_DIR, 'generate.py')
generate = imp.load_source('generate', GENERATE_FILEPATH)

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

    def testEmptyArchive(self):
        # Run the generator.
        generate.main(
            [
                "--output",
                TMP_DIR_NAME,
                "--directory",
                os.path.join(SCRIPT_DIR, 'testdata'),
            ])
        self.verify_contents(TMP_DIR_NAME)

    def verify_contents(self, outdir):
        dcmp = filecmp.dircmp(
            outdir, os.path.join(SCRIPT_DIR, 'golden'), ignore=['bin', 'build'])
        self.verify_contents_recursive(dcmp)
        # check the test_targets template. This is in the build directory,
        # but since it is generated vs. static we add another comparision.
        # fxb/45207 is  tracking fixing the comparison to only look at
        # all generated files.
        generated_file = os.path.join(outdir,'build', 'test_targets.gni')
        golden_file = os.path.join(
            SCRIPT_DIR, 'golden','build','test_targets.gni')
        if not filecmp.cmp(generated_file, golden_file, False):
            self.fail("Generated %s does not match : %s." %
             (generated_file, golden_file))


    def verify_contents_recursive(self, dcmp):
        """Recursively checks for differences between two directories.

        Fails if the directories do not appear to be deeply identical in
        structure and content.

        Args:
            dcmp (filecmp.dircmp): A dircmp of the directories.
        """
        if dcmp.left_only or dcmp.right_only:
            self.fail("Generated SDK does not match golden files. " \
                "You can run ./update_golden.py to update them.\n" \
                "Only in {}:\n{}\n\n" \
                "Only in {}:\n{}\n\n"
                .format(dcmp.left, dcmp.left_only, dcmp.right, dcmp.right_only))
        elif dcmp.diff_files:
            # Show a diff of the culprit files. Need to run diff for each pair.
            diff_result = ''
            for file in dcmp.diff_files:
                cmd_args = ['diff', os.path.join(dcmp.left, file),
                    os.path.join(dcmp.right, file)]
                pipe = subprocess.Popen(cmd_args, stdout=subprocess.PIPE)
                out, err = pipe.communicate()
                diff_result += "diff of '{}':\n{}\n".format(file, out)
            self.fail("Generated SDK does not match golden files. " \
                "You can run ./update_golden.py to update them.\n" \
                "Left : {}\n" \
                "Right: {}\n" \
                "Different files: {}\n" \
                "{}"
                .format(dcmp.left, dcmp.right, dcmp.diff_files, diff_result))

        for sub_dcmp in dcmp.subdirs.values():
            self.verify_contents_recursive(sub_dcmp)


def TestMain():
    unittest.main()


if __name__ == '__main__':
    TestMain()
