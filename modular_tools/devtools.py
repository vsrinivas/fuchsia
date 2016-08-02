# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper module that allows to put the devtools library in PATH as needed."""

import os
import sys


def add_lib_to_path():
  """Adds the devtools pylib to PATH."""
  sys.path.append(os.path.join(os.path.dirname(__file__),
                               os.pardir,
                               "third_party",
                               "mojo_devtools"))
