#!/usr/bin/python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(fxbug.dev/10680): switch to the standard shebang line when the mocking library
# is available.

import os
import sys
import unittest
from unittest.mock import patch, MagicMock

from sdk_common import Atom, AtomId, detect_category_violations


def _atom(name, category):
    return Atom(
        {
            'id': {
                'domain': 'foo',
                'name': name,
            },
            'category': category,
            'gn-label': '//hello',
            'deps': [],
            'package-deps': [],
            'files': [],
            'tags': [],
        })


class SdkCommonTests(unittest.TestCase):

    def test_categories(self):
        atoms = [_atom('hello', 'internal'), _atom('world', 'public')]
        self.assertFalse(detect_category_violations('internal', atoms))

    def test_categories_failure(self):
        atoms = [_atom('hello', 'internal'), _atom('world', 'public')]
        self.assertTrue(detect_category_violations('partner', atoms))

    def test_category_name_bogus(self):
        atoms = [_atom('hello', 'foobarnotgood'), _atom('world', 'public')]
        self.assertRaises(
            Exception, detect_category_violations, 'partner', atoms)


if __name__ == '__main__':
    unittest.main()
