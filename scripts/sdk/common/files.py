#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import os
import shutil


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


def copy_tree(src, dst):
    '''Recursively copies a directory into another.
    Differs with shutil.copytree in that it won't fail if the destination
    directory already exists.
    '''
    if not os.path.isdir(dst):
        os.mkdir(dst)
    for path, directories, files in os.walk(src):
        def get_path(name):
            source_path = os.path.join(path, name)
            dest_path = os.path.join(dst, os.path.relpath(source_path, src))
            return (source_path, dest_path)
        for dir in directories:
            source, dest = get_path(dir)
            if not os.path.isdir(dest):
                os.mkdir(dest)
        for file in files:
            source, dest = get_path(file)
            shutil.copy2(source, dest)
