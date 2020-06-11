#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import TestCase


class DictionaryTest(TestCase):

    def setUp(self):
        super(DictionaryTest, self).setUp()
        self.create_fuzzer('check', 'fake-package1/fake-target1')
        self.ns = self.fuzzer.ns
        self.dictionary = self.fuzzer.dictionary

    def test_replace(self):
        local_dict = 'local_dict'
        self.cli.touch(local_dict)
        self.assertEqual(self.dictionary.nspath, self.ns.resource('dictionary'))
        self.dictionary.replace(local_dict)
        self.assertEqual(self.dictionary.nspath, self.ns.data(local_dict))
        self.assertScpTo(local_dict, self.data_abspath(local_dict))


if __name__ == '__main__':
    unittest.main()
