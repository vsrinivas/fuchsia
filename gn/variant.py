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
        'aux',              # List of target files required if this is used.
    ])):

    def matches(self, info, assume=False):
        if self.libprefix and info.interp:
            return info.interp.startswith(self.libprefix)
        if self.runtime:
            return self.runtime in info.needed
        return assume


def make_variant(name, info):
    if name is None:
        tc = '%s-shared' % info.cpu.gn
    else:
        tc = '%s-%s-shared' % (info.cpu.gn, name)
        if name in ('asan', 'asan-sancov'):
            return variant(tc,
                           'asan/',
                           'libclang_rt.asan-%s.so' % info.cpu.llvm,
                           ['bin/devhost.asan'] if is_driver(info) else [])
    return variant(tc, '', None, [])


def find_variant(info, build_dir=''):
    dir, file = os.path.split(info.filename)
    variant = None
    variant_file = None
    if dir == build_dir:
        # It's in the build directory.  If it's a variant, it's a hard link
        # into the variant toolchain root_out_dir.
        file_stat = os.stat(info.filename)
        if file_stat.st_nlink > 1:
            # Figure out which variant it's linked to.
            variant_prefix = info.cpu.gn + '-'
            files = filter(
                lambda file: os.path.samestat(os.stat(file), file_stat),
                glob.glob(os.path.join(build_dir, variant_prefix + '*', file)))
            # Rust binaries have multiple links but are not variants.
            # So just ignore a multiply-linked file with no matches.
            if files:
                assert len(files) == 1, (
                    "Multiple hard links to %r: %r" % (info, files))
                variant_file = os.path.relpath(files[0], build_dir)
                dir = os.path.basename(os.path.dirname(files[0]))
                name = dir[len(variant_prefix):]
                # Nonvariant drivers are linked to the -shared toolchain,
                # but that's not a variant.
                if name != 'shared':
                    # Drivers are linked to the variant-shared toolchain.
                    if name[-7:] == '-shared':
                        name = name[:-7]
                    variant = make_variant(name, info)
    else:
        # It's from an auxiliary.
        asan = make_variant('asan', info)
        if asan.matches(info):
            variant = asan
    if variant:
        assert variant.matches(info), "%r vs %r" % (variant, info)
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
