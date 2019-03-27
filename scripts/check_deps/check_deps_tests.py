#!/usr/bin/env python

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import check_deps
import shutil
import tempfile
import unittest

areas = [
    'src/rockets',
    'src/rockets/nozzles',
    'src/planets',
]

# Test case format: [ label, expected_area ]
area_for_label_cases = [
    ['', ''],
    ['/', ''],
    ['//', ''],
    ['//:root_target', ''],
    ['//src', ''],
    ['//src/', ''],
    ['//src/rockets', '//src/rockets'],
    ['//src/rockets/nozzles/core', '//src/rockets/nozzles'],
    ['//src/rockets:foo', '//src/rockets'],
    ['//src/planets:ledger_tests.manifest', '//src/planets'],
    ['//something/public/lib/fxl:fxl', ''],
]

# Test case format: [ label, dependency ]
# Allowable dependency patterns from layout.md:
#   //build
#   //buildtools
#   //sdk
#   //third_party
#   (../)+lib
allowable_dep_cases = [
    ['//src/rockets/nozzles', '//build/toolchain:something'],
    ['//src/rockets/nozzles', '//buildtools/fuel_meter'],
    ['//src/rockets:foo', '//sdk/lib/widgets'],
    ['//src/planets/star_data', '//third_party/astrolabe'],
    ['//src/rockets/nozzles',
        '//src/lib/nova:lib(//buildtools/toolchain:some_other_toolchain)'],
]

# Test case format: [ label, dependency ]
disallowed_dep_cases = [
    ['//src/rockets:foo', '//src/planets:bar'],
    ['//src/rockets/nozzles', '//something/lib/foobar'],
    ['//src/rockets/tests:rocket_tests', '//src/rockets/nozzles/lib:stardata'],
    ['//src/rockets/nozzles/widgets:helper', '//src/rockets/libations/qux'],
]


class TestCheckdepsMethods(unittest.TestCase):

    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        for area in areas:
            area_owners_file = os.path.join(self.test_dir, area, 'OWNERS')
            os.makedirs(os.path.dirname(area_owners_file))
            with open(area_owners_file, 'w') as f:
                f.write('\n')

    def tearDown(self):
        shutil.rmtree(self.test_dir)

    def test_area_for_label(self):
        for test in area_for_label_cases:
            label_area = check_deps.area_for_label(self.test_dir, test[0])
            self.assertEquals(label_area, test[1])

    def test_dep_allowed(self):
        ignore_exclusions = False
        for case in allowable_dep_cases:
            label = case[0]
            label_area = check_deps.area_for_label(self.test_dir, label)
            dep = case[1]
            dep_area = check_deps.area_for_label(self.test_dir, dep)
            msg = '%s should be allowed to depend on %s' % (label, dep)
            allowed = check_deps.dep_allowed(label, label_area, dep,
                                             dep_area, ignore_exclusions)
            self.assertTrue(allowed, msg=msg)

    def test_dep_disallowed(self):
        ignore_exclusions = False
        for case in disallowed_dep_cases:
            label = case[0]
            label_area = check_deps.area_for_label(self.test_dir, label)
            dep = case[1]
            dep_area = check_deps.area_for_label(self.test_dir, dep)
            msg = '%s should not be allowed to depend on %s' % (label, dep)
            allowed = check_deps.dep_allowed(label, label_area, dep,
                                             dep_area, ignore_exclusions)
            self.assertFalse(allowed, msg=msg)


if __name__ == '__main__':
    unittest.main()
