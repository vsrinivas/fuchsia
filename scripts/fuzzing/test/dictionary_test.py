#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

import test_env
from test_case import TestCaseWithFuzzer


class DictionaryTest(TestCaseWithFuzzer):

    @property
    def dictionary(self):
        return self.fuzzer.dictionary

    def test_find_on_device(self):
        resource = self.ns.resource(self.fuzzer.executable + '/dictionary')

        self.dictionary.find_on_device()
        self.assertFalse(self.dictionary.nspath)

        self.touch_on_device(self.ns.resource_abspath(resource))
        self.dictionary.find_on_device()
        self.assertEqual(self.dictionary.nspath, resource)

    def test_replace(self):
        resource = self.ns.resource(self.fuzzer.executable + '/dictionary')

        self.touch_on_device(self.ns.resource_abspath(resource))
        self.assertEqual(self.dictionary.nspath, resource)

        # Missing local replacement
        local_dict = 'local_dict'
        self.assertError(
            lambda: self.dictionary.replace(local_dict),
            'No such file: {}'.format(local_dict))
        self.host.touch(local_dict)

        # Valid
        relpath = local_dict
        self.assertEqual(self.dictionary.nspath, resource)
        self.dictionary.replace(local_dict)
        self.assertEqual(self.dictionary.nspath, self.ns.data(relpath))
        self.assertScpTo(local_dict, self.ns.data_abspath(relpath))


if __name__ == '__main__':
    unittest.main()
