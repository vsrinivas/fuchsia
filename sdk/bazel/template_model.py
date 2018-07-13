#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''A collection of storage classes to use with Mako templates.'''

class _CppLibrary(object):

    def __init__(self, name):
        self.name = name
        self.hdrs = []
        self.deps = []
        self.includes = []


class CppSourceLibrary(_CppLibrary):

    def __init__(self, name):
        super(CppSourceLibrary, self).__init__(name)
        self.srcs = []


class CppPrebuiltLibrary(_CppLibrary):

    def __init__(self, name, target_arch):
        super(CppPrebuiltLibrary, self).__init__(name)
        self.prebuilt = ""
        self.is_static = False
        self.target_arch = target_arch
        self.packaged_files = {}


class Sysroot(object):

    def __init__(self, target_arch):
        self.target_arch = target_arch
        self.packaged_files = {}


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
