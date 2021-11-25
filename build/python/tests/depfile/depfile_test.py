#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
import tempfile

from depfile import DepFile


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

    expected = "baz/output: \\\n  ../input_c \\\n  things/input_a \\\n  things/input_b\n"

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

        depfile = DepFile(output)
        depfile.update([input_a, input_b, input_c])

        self.assertEqual(str(depfile), DepFileTests.expected)

    def test_depfile_writing(self):
        depfile = DepFile("/foo/bar/baz/output", rebase="/foo/bar")
        depfile.update(
            [
                "/foo/bar/things/input_a", "/foo/bar/things/input_b",
                "/foo/input_c"
            ])

        with tempfile.TemporaryFile('w+') as outfile:
            # Write out the depfile
            depfile.write_to(outfile)

            # Read the contents back in
            outfile.seek(0)
            contents = outfile.read()
            self.assertEqual(contents, DepFileTests.expected)


if __name__ == '__main__':
    unittest.main()
