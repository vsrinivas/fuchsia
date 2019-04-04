#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import test_env
from lib.fuzzer import Fuzzer

from device_mock import MockDevice
from host_mock import MockHost


class TestFuzzer(unittest.TestCase):

  def test_filter(self):
    host = MockHost()
    fuzzers = host.fuzzers
    self.assertEqual(len(Fuzzer.filter(fuzzers, '')), 4)
    self.assertEqual(len(Fuzzer.filter(fuzzers, '/')), 4)
    self.assertEqual(len(Fuzzer.filter(fuzzers, 'mock')), 4)
    self.assertEqual(len(Fuzzer.filter(fuzzers, 'package1')), 3)
    self.assertEqual(len(Fuzzer.filter(fuzzers, 'target1')), 2)
    self.assertEqual(len(Fuzzer.filter(fuzzers, '1/2')), 1)
    self.assertEqual(len(Fuzzer.filter(fuzzers, 'target4')), 0)
    with self.assertRaises(Fuzzer.NameError):
      Fuzzer.filter(fuzzers, 'a/b/c')

  def test_measure_corpus(self):
    fuzzer = Fuzzer(MockDevice(), u'mock-package1', u'mock-target1')
    sizes = fuzzer.measure_corpus()
    self.assertEqual(sizes[0], 2)
    self.assertEqual(sizes[1], 1796 + 124)

  def test_list_artifacts(self):
    fuzzer = Fuzzer(MockDevice(), u'mock-package1', u'mock-target1')
    artifacts = fuzzer.list_artifacts


if __name__ == '__main__':
  unittest.main()
