# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import unittest


class CodePointsTestCase(unittest.TestCase):
    """Tests for code_points.py"""

    main_path = os.path.normpath(
        os.path.join(os.path.realpath(__file__), '..', '..', 'code_points.py'))

    sample_font_ttf_path = os.path.normpath(
        os.path.join(os.path.realpath(__file__), '..', 'sample_font.ttf'))

    def test_golden_output(self):
        result = subprocess.run([self.main_path, self.sample_font_ttf_path],
                                stdout=subprocess.PIPE)
        output = result.stdout.decode().strip()
        self.assertEqual(output, '0,13,19,8+1,3,4+2,15+5')


if __name__ == '__main__':
    unittest.main()
