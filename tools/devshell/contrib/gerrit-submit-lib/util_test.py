#!/usr/bin/python3
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

from typing import List

import util


class TestUtil(unittest.TestCase):

  def test_exponential_backoff(self) -> None:
    clock = util.FakeClock()
    backoff = util.ExponentialBackoff(
        clock, min_poll_seconds=1., max_poll_seconds=10., backoff=2.)
    backoff.wait()
    backoff.wait()
    backoff.wait()
    backoff.wait()
    backoff.wait()
    backoff.wait()
    self.assertEqual(clock.pauses, [1., 2., 4., 8., 10., 10.])


if __name__ == '__main__':
  unittest.main()

