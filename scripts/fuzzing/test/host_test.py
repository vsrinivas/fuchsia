#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
import tempfile

import test_env
from lib.host import Host
from host_mock import MockHost


class TestHost(unittest.TestCase):

  def test_zircon_tool(self):
    host = MockHost()
    path = os.path.abspath(__file__)
    line = host.zircon_tool(['merkleroot', path])
    self.assertRegexpMatches(line, r'[0-9a-f]* - ' + path)
    with self.assertRaises(OSError):
      host.zircon_tool(['no_such_tool'])


if __name__ == '__main__':
  unittest.main()
