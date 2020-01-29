#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''A collection of storage classes to use with Mako templates.'''

ARCH_MAP = {
    'arm64': 'aarch64',
    'x64': 'x86_64',
}


class _CppLibrary(object):

    def __init__(self, name):
        self.name = name
        self.hdrs = []
        self.deps = []
        self.includes = ''


class CppSourceLibrary(_CppLibrary):

    def __init__(self, name):
        super(CppSourceLibrary, self).__init__(name)
        self.srcs = []


class CppPrebuiltSet(object):

    def __init__(self, link):
        self.link_lib = link
        self.dist_lib = ''
        self.dist_path = ''


class CppPrebuiltLibrary(_CppLibrary):

    def __init__(self, name):
        super(CppPrebuiltLibrary, self).__init__(name)
        self.prebuilt = ""
        self.is_static = False
        self.packaged_files = {}
        self.prebuilts = {}


class FidlLibrary(object):

    def __init__(self, name, library):
        self.name = name
        self.library = library
        self.srcs = []
        self.deps = []
        self.with_cc = False
        self.with_dart = False


class Arch(object):

    def __init__(self, short, long):
        self.short_name = short
        self.long_name = long


class Crosstool(object):

    def __init__(self, arches=[]):
        self.arches = []
        for arch in arches:
            if arch in ARCH_MAP:
                self.arches.append(Arch(arch, ARCH_MAP[arch]))
            else:
                print('Unknown target arch: %s' % arch)


class DartLibrary(object):

    def __init__(self, name, package):
        self.name = name
        self.package_name = package
        self.deps = []


class TestWorkspace(object):

    def __init__(self, sdk_path, with_cc, with_dart):
        self.sdk_path = sdk_path
        self.with_cc = with_cc
        self.with_dart = with_dart
