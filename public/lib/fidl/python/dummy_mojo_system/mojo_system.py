# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This module sits in the place of mojo_system when mojo_system is not needed.

This makes it possible to use mojom serialization/deserialization without
building mojo_system.

In order to use this module, simply make sure it can be found on the python
path before loading any generated mojom modules.
"""

import sys


class DummyMojoSystem(object):
  def __getattr__(self, attr):
    raise Exception("The dummy mojo_system does not support handles, interface "
                    "requests or message pipes.")


# Replaces this module with an instance of DummyMojoSystem.
sys.modules[__name__] = DummyMojoSystem()
