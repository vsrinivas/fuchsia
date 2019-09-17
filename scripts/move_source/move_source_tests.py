#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import move_source
import os
import shutil
import subprocess
import tempfile
import unittest


class TestMoveSources(unittest.TestCase):

    def test_find_referenced_targets(self):
        build_graph = json.loads('''
{
  "//source/lib/used:used": {
      "deps": [ ],
      "public": "*",
      "public_configs": [ ],
      "testonly": false
   },
   "//source/lib/used:used_tests": {
      "deps": [ ],
      "public": "*",
      "public_configs": [ ],
      "testonly": true
   },
   "//source/lib/unused:unused": {
     "deps": [ ],
     "testonly": false
   },
   "//dest/foo:foo_bin": {
      "configs": [  ],
      "deps": [ "//source/lib/used:used", "//build/config/scudo:default_for_executable" ],
      "sources": [ "//dest/foo/main.cc" ],
      "testonly": false
   },
   "//dest/foo:foo_tests": {
      "deps": [ "//source/lib/used:used_tests", "//build/config/scudo:default_for_executable", "//third_party/googletest:gtest" ],
      "sources": [ "//dest/foo/tests.cc" ],
      "testonly": true
    }
}
        ''')
        source = "source/lib"
        dest = "something/else/lib"
        actual = move_source.find_referenced_targets(build_graph, source)
        expected = [
            ("//source/lib/used:used", False),
            ("//source/lib/used:used_tests", True),
        ]
        self.assertEquals(expected, actual)

    def test_move_directory(self):
        temp_dir = tempfile.mkdtemp()
        try:
            subprocess.check_call(['git', 'init'], cwd=temp_dir)
            source = os.path.join('nested', 'source')
            source_abs = os.path.join(temp_dir, source)
            os.makedirs(source_abs)
            source_file = os.path.join(source_abs, 'foo.txt')
            with open(source_file, 'a') as f:
                f.write('\n')
            subprocess.check_call(['git', 'add', source_file], cwd=temp_dir)
            dest = os.path.join('different', 'nested', 'destination')

            dry_run = False
            move_source.move_directory(source, dest, dry_run, temp_dir)
        finally:
            shutil.rmtree(temp_dir)
        pass


if __name__ == '__main__':
    unittest.main()
