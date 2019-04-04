#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

import test_env
from lib.host import Host


class MockHost(Host):

  def __init__(self):
    super(MockHost, self).__init__()
    self.fuzzers = [(u'mock-package1', u'mock-target1'),
                    (u'mock-package1', u'mock-target2'),
                    (u'mock-package1', u'mock-target3'),
                    (u'mock-package2', u'mock-target1')]
