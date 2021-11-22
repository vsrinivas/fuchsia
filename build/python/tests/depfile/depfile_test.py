#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import argparse
import os
import unittest
from depfile import DepFile

global OUTFILE_NAME


class DepFileTests(unittest.TestCase):
    """Validate the depfile generation

    This validates the rebasing behavior using the following imaginary set of
    files::

        /foo/
             bar/
                 baz/
                     output
                 things/
                        input_a
                        input_b
             input_c

    Assume a CWD of /foo/bar
    """

    expected = "baz/output: ../input_c things/input_a things/input_b\n"

    def test_specified_cwd(self):

        output = "/foo/bar/baz/output"
        input_a = "/foo/bar/things/input_a"
        input_b = "/foo/bar/things/input_b"
        input_c = "/foo/input_c"

        rebased_depfile = DepFile(output, rebase="/foo/bar")
        rebased_depfile.add_input(input_a)
        rebased_depfile.add_input(input_b)
        rebased_depfile.update([input_b, input_c])

        self.assertEqual(str(rebased_depfile), DepFileTests.expected)

    def test_inferred_cwd(self):
        """Validate the standard behavior, with a mix of absolute and real paths."""

        # make the output absolute (from a path relative to the cwd)
        output = os.path.abspath("baz/output")
        input_a = os.path.abspath("things/input_a")
        input_b = "things/input_b"
        input_c = os.path.abspath("../input_c")

        dep_file = DepFile(output)
        dep_file.update([input_a, input_b, input_c])

        self.assertEqual(str(dep_file), DepFileTests.expected)

    def test_depfile_writing(self):
        depfile = DepFile("/foo/bar/baz/output", rebase="/foo/bar")
        depfile.update(
            [
                "/foo/bar/things/input_a", "/foo/bar/things/input_b",
                "/foo/input_c"
            ])

        with open(OUTFILE_NAME, 'w') as outfile:
            depfile.write_to(outfile)

        with open(OUTFILE_NAME, 'r') as outfile:
            contents = outfile.read()
            self.assertEqual(contents, str(depfile))


if __name__ == '__main__':
    parser = argparse.ArgumentParser('Test depfile creation')
    parser.add_argument("--outfile")
    args = parser.parse_args()
    OUTFILE_NAME = args.outfile

    unittest.main(argv=[sys.argv[0]])
