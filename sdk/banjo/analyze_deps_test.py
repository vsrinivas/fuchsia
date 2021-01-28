#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

from analyze_deps import extract_dependencies, filter_banjo_libaries, get_library_label


class AnalysisTests(unittest.TestCase):

    def test_library_label(self):
        self.assertEqual(
            get_library_label(
                '//sdk/banjo/fuchsia.hardware.foo:fuchsia.hardware.foo'),
            'fuchsia.hardware.foo')
        self.assertEqual(
            get_library_label(
                '//sdk/banjo/fuchsia.hardware.foo:fuchsia.hardware.foo...'),
            'fuchsia.hardware.foo')
        self.assertEqual(get_library_label('//src/lib/zircon:zircon'), None)

    def test_extract_dependencies_simple(self):
        deps = [
            '//one',
            '//two',
        ]
        result = extract_dependencies(deps)
        self.assertCountEqual(result['main'], ['//one', '//two'])

    def test_extract_dependencies_more_depth(self):
        deps = [
            '//one',
            '  //one/a',
            '  //one/b',
            '  //one/c',
            '//two',
            '  //one...',
            '  //two/a',
        ]
        result = extract_dependencies(deps)
        self.assertCountEqual(result['main'], ['//one', '//two'])
        self.assertCountEqual(
            result['//one'], ['//one/a', '//one/b', '//one/c'])
        self.assertCountEqual(result['//two'], ['//one', '//two/a'])

    def test_extract_dependencies_even_more_depth(self):
        deps = [
            '//one',
            '  //one/a',
            '  //one/b',
            '    //one/b/foo',
            '    //one/b/bar',
            '  //one/c',
        ]
        result = extract_dependencies(deps)
        self.assertCountEqual(result['main'], ['//one'])
        self.assertCountEqual(
            result['//one'], ['//one/a', '//one/b', '//one/c'])
        self.assertCountEqual(result['//one/b'], ['//one/b/foo', '//one/b/bar'])

    def test_filter_banjo_libraries(self):
        deps = {
            '//foo': ['//foo/a', '//foo/b'],
            'main':
                [
                    '//sdk/banjo/one:one', '//sdk/banjo/two:two',
                    '//sdk/banjo/three:three', '//sdk/banjo/four:four',
                    '//sdk/banjo/five:five'
                ],
            '//sdk/banjo/one:one': ['//sdk/banjo/two:two', '//bar'],
            '//sdk/banjo/two:two':
                ['//sdk/banjo/three:three', '//sdk/banjo/four:four']
        }
        deps = filter_banjo_libaries(deps)
        self.assertCountEqual(
            deps.keys(), ['one', 'two', 'three', 'four', 'five'])
        self.assertCountEqual(deps['one'], ['two'])
        self.assertCountEqual(deps['two'], ['three', 'four'])
        self.assertCountEqual(deps['three'], [])
        self.assertCountEqual(deps['four'], [])
        self.assertCountEqual(deps['five'], [])


if __name__ == "__main__":
    unittest.main()
