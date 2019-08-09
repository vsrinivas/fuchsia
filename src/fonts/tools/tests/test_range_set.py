# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import unittest

from range_set import RangeSet


class RangeSetTestCase(unittest.TestCase):
    """Tests for RangeSet."""

    def test_to_offset_string(self):
        r = RangeSet(ranges=[range(0, 5), range(9, 10), range(20, 29)])
        self.assertEqual('0+4,5,11+8', r.to_offset_string())

    def test_to_offset_string_skip_zero(self):
        r = RangeSet(ranges=[range(3, 5), range(9, 10), range(20, 29)])
        self.assertEqual('3+1,5,11+8', r.to_offset_string())

    def test_from_offset_string(self):
        self.assertEqual(
            RangeSet(ranges=[range(0, 5), range(9, 10), range(20, 29)]),
            RangeSet.from_offset_string('0+4,5,11+8'))


if __name__ == '__main__':
    unittest.main()
