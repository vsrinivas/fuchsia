#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import os


def make_dir(file_path):
    '''Creates the directory hierarchy for the given file and returns the
    given path.
    '''
    target = os.path.dirname(file_path)
    try:
        os.makedirs(target)
    except OSError as exception:
        if exception.errno == errno.EEXIST and os.path.isdir(target):
            pass
        else:
            raise
    return file_path
