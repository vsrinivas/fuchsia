#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import platform

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
FUCHSIA_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, os.pardir, os.pardir))
GN_PATH = os.path.join(FUCHSIA_ROOT, "buildtools", "gn")
BUILDTOOLS_PATH = os.path.join(FUCHSIA_ROOT, "buildtools", '%s-%s' % (
    platform.system().lower().replace('darwin', 'mac'),
    {
        'x86_64': 'x64',
        'aarch64': 'arm64',
    }[platform.machine()],
))
DEBUG_OUT_DIR = os.path.join(FUCHSIA_ROOT, "out", "debug-x64")
RELEASE_OUT_DIR = os.path.join(FUCHSIA_ROOT, "out", "release-x64")

_BUILD_TOOLS = {}

def build_tool(package, tool):
    """Return the full path of TOOL binary in PACKAGE.

    This will raise an assertion failure if the binary doesn't exist.
    This function memoizes its results, so there's not much need to
    cache its results in calling code.
    """

    path = _BUILD_TOOLS.get((package, tool))
    if path is None:
        path = os.path.join(BUILDTOOLS_PATH, package, 'bin', tool)
        assert os.path.exists(path), "No '%s' tool in '%s'" % (tool, package)
        _BUILD_TOOLS[package, tool] = path
    return path
