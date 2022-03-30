#!/usr/bin/env fuchsia-vendored-python
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import mem_digest
import os
import unittest
import sys
from contextlib import redirect_stdout
import io

parser = argparse.ArgumentParser()
parser.add_argument(
    '--test_dir_path', help='Path to the test data directory.', required=True)
args = parser.parse_args()

TEST_DIR_PATH = args.test_dir_path

# The python_host_test build rule calls `unittest.main`.
# So we need to get rid of the test arguments in order
# to prevent them from interfering with `unittest`'s args.
#
# Pop twice to get rid of the `--test_dir_path` flag and
# its value.
sys.argv.pop()
sys.argv.pop()


class MainArgParserTests(unittest.TestCase):

    def test_digest(self):
        parser = mem_digest.get_arg_parser()
        snapshot_path = os.path.join(TEST_DIR_PATH, "test_snapshot.json")
        digest_path = os.path.join(TEST_DIR_PATH, "test_digest.json")
        csv_path = os.path.join(TEST_DIR_PATH, "test_output.csv")
        args = parser.parse_args(
            [
                "--snapshot", snapshot_path, "--digest", digest_path,
                "--output=csv"
            ])

        stdout = run_capture_output(mem_digest.main, args)
        with open(csv_path) as csv:
            self.assertEqual(stdout, csv.read())

    def test_digest_human(self):
        parser = mem_digest.get_arg_parser()
        snapshot_path = os.path.join(TEST_DIR_PATH, "test_snapshot.json")
        digest_path = os.path.join(TEST_DIR_PATH, "test_digest.json")
        human_path = os.path.join(TEST_DIR_PATH, "test_human_output.txt")
        args = parser.parse_args(
            [
                "--snapshot", snapshot_path, "--digest", digest_path,
                "--output=human"
            ])

        stdout = run_capture_output(mem_digest.main, args)
        with open(human_path) as human:
            self.assertEqual(stdout, human.read())

    def test_fmt_size(self):
        self.assertEqual(mem_digest.fmt_size(0), "0B")
        self.assertEqual(mem_digest.fmt_size(1023), "1023B")
        self.assertEqual(mem_digest.fmt_size(1024), "1KiB")
        self.assertEqual(mem_digest.fmt_size(1024 + 511), "1.499KiB")
        self.assertEqual(mem_digest.fmt_size(1024 * 1024), "1MiB")
        self.assertEqual(mem_digest.fmt_size(32 * 1024 * 1024), "32MiB")
        self.assertEqual(mem_digest.fmt_size(10248192), "9.773MiB")


def run_capture_output(f, arguments):
    f = io.StringIO()
    with redirect_stdout(f):
        mem_digest.main(arguments)
    return f.getvalue()


if __name__ == '__main__':
    unittest.main()
