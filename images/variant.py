#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import namedtuple
import elfinfo
import glob
import os


# Copied from //zircon/system/public/zircon/driver/binding.h, which see.
ZIRCON_NOTE_DRIVER = 0x31565244 # DRV1
ZIRCON_DRIVER_IDENT = ('Zircon\0', ZIRCON_NOTE_DRIVER)


def binary_info(filename):
    return elfinfo.get_elf_info(filename, [ZIRCON_DRIVER_IDENT])

def is_driver(info):
    return bool(info.notes)


class variant(
    namedtuple('variant', [
        'shared_toolchain', # GN toolchain (and thus build dir subdirectory).
        'libprefix',        # Prefix on DT_SONAME string.
        'runtime',          # SONAME of runtime, does not use libprefix.
        'aux',              # List of (file, group) required if this is used.
    ])):

    def matches(self, info, assume=False):
        if self.libprefix and info.interp:
            return info.interp.startswith(self.libprefix)
        if self.runtime:
            return self.runtime in info.needed
        return assume


def make_variant(name, info):
    libprefix = ''
    runtime = None
    # All drivers need devhost; it must be in /boot (group 0).
    aux = [('bin/devhost', 0)] if is_driver(info) else []
    if name is None:
        tc = '%s-shared' % info.cpu.gn
    else:
        tc = '%s-%s-shared' % (info.cpu.gn, name)
        if name in ('asan', 'asan-sancov'):
            libprefix = 'asan/'
            runtime = 'libclang_rt.asan.so'
            # ASan drivers need devhost.asan.
            aux = [(file + '.asan', group) for file, group in aux]
    return variant(tc, libprefix, runtime, aux)


def find_variant(info, build_dir=os.path.curdir):
    variant = None
    variant_file = None
    abs_build_dir = os.path.abspath(build_dir)
    abs_filename = os.path.abspath(info.filename)
    if abs_filename.startswith(os.path.join(abs_build_dir, '')):
        # It's in the build directory.  If it's a variant, it's a hard link
        # into the variant toolchain root_out_dir.
        file_stat = os.stat(info.filename)
        if file_stat.st_nlink > 1:
            # Figure out which variant it's linked to.  Non-variant drivers
            # are linked to the -shared toolchain.  We match those as well
            # as actual variants so we'll replace the unadorned filename
            # with its -shared/ version, which is where the lib.unstripped/
            # subdirectory is found.  Below, we'll change the name but not
            # call it a variant.
            rel_filename = os.path.relpath(abs_filename, abs_build_dir)
            variant_prefix = info.cpu.gn + '-'
            subdirs = [subdir
                       for subdir in os.listdir(build_dir)
                       if (subdir.startswith(variant_prefix) and
                           os.path.exists(os.path.join(subdir, rel_filename)))]
            files = [os.path.join(subdir, rel_filename) for subdir in subdirs]
            assert all(os.path.samestat(os.stat(file), file_stat)
                       for file in files), (
                "Not all %r matches are hard links: %r" % (info, files))
            # Rust binaries have multiple links but are not variants.
            # So just ignore a multiply-linked file with no matches.
            if files:
                # A variant loadable_module (or driver_module) is actually
                # built in the variant's -shared toolchain but is also
                # linked to other variant toolchains.
                if (len(subdirs) > 1 and
                    sum(subdir.endswith('-shared') for subdir in subdirs) == 1):
                    [subdir] = [subdir for subdir in subdirs
                                if subdir.endswith('-shared')]
                else:
                    assert len(files) == 1, (
                        "Multiple hard links to %r: %r" % (info, files))
                    [subdir] = subdirs
                name = subdir[len(variant_prefix):]
                variant_file = os.path.relpath(
                    os.path.join(subdir, rel_filename), build_dir)
                if name != 'shared':
                    # loadable_module and driver_module targets are linked
                    # to the variant-shared toolchain.
                    if name[-7:] == '-shared':
                        name = name[:-7]
                    variant = make_variant(name, info)
    else:
        # It's from an auxiliary.
        asan = make_variant('asan', info)
        if asan.matches(info):
            variant = asan
    if variant:
        assert variant.matches(info, True), "%r vs %r" % (variant, info)
        return variant, variant_file
    return make_variant(None, info), variant_file


# Module public API.
__all__ = ['binary_info', 'find_variant', 'variant']


def test_main(build_dir, filenames):
    for filename in filenames:
        info = binary_info(filename)
        print info
        print '  Driver: %r' % is_driver(info)
        print '  %r' % (find_variant(info, build_dir),)


# For manual testing.
if __name__ == "__main__":
    import sys
    test_main(sys.argv[1], sys.argv[2:])
