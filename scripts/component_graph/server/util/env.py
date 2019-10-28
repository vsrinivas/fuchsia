#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
""" Provides OS environment utility functionality. """

import os


def get_fuchsia_root():
    """Returns the fuchsia the from the environment."""
    fuchsia_dir = os.environ.get("FUCHSIA_DIR")
    if not os.path.exists(fuchsia_dir):
        return None
    if not fuchsia_dir.endswith("/"):
        fuchsia_dir += "/"
    return fuchsia_dir
