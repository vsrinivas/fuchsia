#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''A collection of storage classes to use with Mako templates.'''

class CppLibrary(object):

    def __init__(self, name, target_arch):
        self.name = name
        self.srcs = []
        self.hdrs = []
        self.deps = []
        self.includes = []
        self.target_arch = target_arch


class FidlLibrary(object):

    def __init__(self, name, library):
        self.name = name
        self.library = library
        self.srcs = []
        self.deps = []


class Arch(object):

    def __init__(self, short, long):
        self.short_name = short
        self.long_name = long


class Crosstool(object):

    def __init__(self):
        self.arches = []


class DartLibrary(object):

    def __init__(self, name, package):
        self.name = name
        self.package_name = package
        self.deps = []
