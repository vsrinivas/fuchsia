#!/usr/bin/env python3
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
        self.longMessage = True
        build_graph = json.loads('''
{
  "//source/lib/used:used": {
      "deps": [ ],
      "public": "*",
      "public_configs": [ ],
      "type": "static_library",
      "testonly": false
   },
   "//source/lib/used:used_tests": {
      "deps": [ ],
      "public": "*",
      "public_configs": [ ],
      "type": "static_library",
      "testonly": true
   },
   "//source/lib/unused:unused": {
     "deps": [ ],
     "type": "group",
     "testonly": false
   },
   "//dest/foo:foo_bin": {
      "configs": [  ],
      "deps": [
        "//source/lib/used:used",
        "//build/config/scudo:default_for_executable"
      ],
      "type": "executable",
      "sources": [ "//dest/foo/main.cc" ],
      "testonly": false
   },
   "//dest/foo:foo_tests": {
      "deps": [
        "//source/lib/used:used_tests",
        "//build/config/scudo:default_for_executable",
        "//third_party/googletest:gtest"
      ],
      "sources": [ "//dest/foo/tests.cc" ],
      "type": "executable",
      "testonly": true
    },
    "//source/lib/go/src/foo:foo": {
      "deps": [ ],
      "sources": [ ],
      "type": "action",
      "script": "//build/go/gen_library_metadata.py",
      "testonly": false
    },
    "//other/go/src/bar:bar": {
      "deps": [ "//source/lib/go/src/foo:foo" ],
      "sources": [ ],
      "type": "action",
      "script": "//build/go/gen_library_metadata.py",
      "testonly": false
    }
}
        ''')
        source = "source/lib"
        dest = "something/else/lib"
        actual = move_source.find_referenced_targets(build_graph, source)
        expected_labels = [
            "//source/lib/used:used_tests",
            "//source/lib/used:used",
            "//source/lib/go/src/foo:foo",
        ]
        expected_testonly = [
            True,
            False,
            False,
        ]
        expected_is_go_library = [
            False,
            False,
            True,
        ]
        self.assertEqual(len(actual), len(expected_labels))
        for i in range(len(expected_labels)):
            self.assertEqual(actual[i].label, expected_labels[i], msg=i)
            self.assertEqual(actual[i].testonly, expected_testonly[i], msg=i)
            self.assertEqual(actual[i].is_go_library,
                              expected_is_go_library[i], msg=i)


    def test_generate_forwarding_target(self):
        self.longMessage = True
        test_cases = [
            {
                "name": "c++ prod target",
                "source": "foo",
                "label": "//foo:cpp",
                "target_json": ''' {
      "label": "//foo:foo",
      "deps": [ ],
      "public": "*",
      "public_configs": [ ],
      "type": "static_library",
      "testonly": false
   }''',
                "dest": "bar",
                "expected_path": "foo/BUILD.gn",
                "expected_imports": set(),
                "expected_snippet": '''
# Do not use this target directly, instead depend on //bar:cpp.
group("cpp") {
  public_deps = [
    "//bar:cpp"
  ]
}
''',
            },
            {
                "name": "c++ test only target",
                "source": "foo",
                "label": "//foo/test_helpers:util",
                "target_json": ''' {
      "label": "//foo/test_helpers:util",
      "deps": [ ],
      "public": "*",
      "public_configs": [ ],
      "type": "static_library",
      "testonly": true
   }''',
                "dest": "bar",
                "expected_path": "foo/test_helpers/BUILD.gn",
                "expected_imports": set(),
                "expected_snippet": '''
# Do not use this target directly, instead depend on //bar/test_helpers:util.
group("util") {
  public_deps = [
    "//bar/test_helpers:util"
  ]
  testonly = true
}
''',
            },
            {
                "name": "go production target",
                "source": "foo",
                "label": "//foo/go/src/foo:foo",
                "target_json": ''' {
      "label": "//foo/go/src/foo:foo",
      "deps": [ ],
      "public": "*",
      "public_configs": [ ],
      "type": "action",
      "script": "//build/go/gen_library_metadata.py",
      "testonly": false
   }''',
                "dest": "bar",
                "expected_path": "foo/go/src/foo/BUILD.gn",
                "expected_imports": set(["//build/go/go_library.gni"]),
                "expected_snippet": '''
# Do not use this target directly, instead depend on //bar/go/src/foo:foo.
go_library("foo") {
  name = "foo_forwarding_target"

  deps = [
    "//bar/go/src/foo:foo"
  ]
}
''',
            },
        ]
        for case in test_cases:
            target_json = case["target_json"]
            target = move_source.ForwardingTarget(case["label"],
                                                  json.loads(target_json))
            abs_path, build = move_source.generate_forwarding_target(
                target, case["source"],
                case["dest"])
            rel_path = os.path.relpath(abs_path, move_source.fuchsia_root)
            self.assertEqual(rel_path, case["expected_path"], msg=case["name"])
            self.assertSetEqual(build.imports, case[
                                "expected_imports"], msg=case["name"])
            self.assertEqual(build.snippet, case[
                              "expected_snippet"], msg=case["name"])


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


if __name__ == '__main__':
    unittest.main()
