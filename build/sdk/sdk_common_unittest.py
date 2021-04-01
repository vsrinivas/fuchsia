#!/usr/bin/python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(fxbug.dev/10680): switch to the standard shebang line when the mocking library
# is available.

import unittest
from sdk_common import Atom, detect_category_violations


def _atom(name, category):
    return Atom(
        {
            'id': {
                'domain': 'foo',
                'name': name,
            },
            'meta': {
                'source': 'foo',
                'dest': 'bar',
            },
            'category': category,
            'gn-label': '//hello',
            'deps': [],
            'package-deps': [],
            'files': [],
            'tags': [],
            'type': 'schema.json'
        })


class SdkCommonTests(unittest.TestCase):

    def test_categories(self):
        atoms = [_atom('hello', 'internal'), _atom('world', 'public')]
        self.assertFalse(detect_category_violations('internal', atoms))
        atoms = [_atom('hello', 'internal'), _atom('world', 'cts')]
        self.assertFalse(detect_category_violations('internal', atoms))

    def test_categories_failure(self):
        atoms = [_atom('hello', 'internal'), _atom('world', 'public')]
        self.assertTrue(detect_category_violations('partner', atoms))
        atoms = [_atom('hello', 'internal'), _atom('world', 'public')]
        self.assertTrue(detect_category_violations('cts', atoms))

    def test_category_name_bogus(self):
        atoms = [_atom('hello', 'foobarnotgood'), _atom('world', 'public')]
        self.assertRaises(
            Exception, detect_category_violations, 'partner', atoms)


if __name__ == '__main__':
    unittest.main()
