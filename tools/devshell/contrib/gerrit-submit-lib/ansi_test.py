#!/usr/bin/python3
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

from typing import List

import ansi


class TestAnsi(unittest.TestCase):

  def test_print_colors(self) -> None:
    for function in [
        ansi.red,
        ansi.green,
        ansi.yellow,
        ansi.gray,
        ansi.bright_green,
    ]:
      print('Printing in %s...' % function(function.__name__))


if __name__ == '__main__':
  unittest.main()
