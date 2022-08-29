#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

from verify_golden_go_proto_file import filter_line


class VerifyGoldenGoProtoFileTests(unittest.TestCase):
    """Validate golden_go_proto file comparisons

  This validates the logic used to compare generated Go for proto files with the
  checked-in goldens for significant differences.
  """

    def test_filter_line(self):
        cases = [
            ('// \tprotoc v1.2.3', '// \tprotoc \n'),
            ('// \tprotoc-gen-go v1.2.3', '// \tprotoc-gen-go \n'),
            ('some text with no    comments', 'some text with no    comments'),
            (
                'a line with //    comments     with       spaces',
                'a line with //comments with spaces',
            ),
            (
                'a line with //comments     with       spaces',
                'a line with //comments with spaces',
            ),
        ]
        for (input, expected) in cases:
            self.assertEqual(filter_line(input), expected)
