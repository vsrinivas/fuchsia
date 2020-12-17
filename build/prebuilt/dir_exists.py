#!/usr/bin/env python3.8

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

if __name__ == '__main__':
  print(os.path.isdir(sys.argv[1]))
