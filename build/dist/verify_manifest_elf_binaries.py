#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Verify the content of ELF binaries embedded in a package or filesystem.
This scripts takes as input a FINI manifest file and on success, will write
an empty stamp file.
"""

# System imports
import argparse
import os
import sys

# elfinfo is in //build/images/ while this script is in //build/dist/.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'images'))
import elfinfo

from typing import Any, Iterable, List, Set, Dict, Tuple, Optional

# The general strategy for checking the ELF binaries within a package is
# the following:
#
# 1) libzircon.so should never appear inside a Fuchsia package, since
#    it is injected by the kernel into user processes at runtime. Its
#    presence in a manifest is a build error.
#
#    On the other hand, it will be listed as a dependency for most
#    binaries, but not necessarily all of them, and should be ignored
#    when that happens.
#
# 2) ld.so.1 is the dynamic loader and also serves as the C library,
#    which means any DT_NEEDED reference to libc.so is automatically
#    resolved at runtime to ld.so.1 instead.
#
#    It is thus a build error to have libc.so inside a Fuchsia package,
#    because the dynamic loader will never load these files!
#
# 3) ELF executables have a PT_INTERP section that will contain 'ld.so.1'
#    for Fuchsia regular binaries, and '<variant>/ld.so.1' for those built
#    with an instrumented variant (e.g. 'asan'). The library loader will
#    search for DT_NEEDED dependencies in '/lib' in the first case,
#    and '/lib/<variant>' in the second case, so check that these are
#    properly located there, as well as their own dependencies.
#
#    For now, it is an error to have a different PT_INTERP value, but
#    this may be relaxed in the future, for example to package Linux
#    and Android executables to be run inside Fuchsia containers.
#
# 4) Loadable modules are ELF shared libraries that are loaded at runtime
#    by binaries (or the Vulkan loader service) through dlopen(), these
#    can be located anywhere in the package. They may have DT_NEEDED
#    dependencies which will be looked up at runtime in the loading
#    executable's own library directory (i.e. '/lib' or '/lib/<variant>').
#
#    Because our build system is flexible enough to group regular and
#    instrumented executables in the same package, there is no 100%
#    fool-proof way to know which executable will load the module.
#
#    Moreover, one package can even contain a standalone module, without
#    its own dependencies, to be loaded from another package's component
#    which would provide them at runtime. This can happen for driver
#    modules for example.
#
#    Due to this, there is no attempt here to verify the dependencies
#    of modules, since doing so can only be performed with heuristics
#    that will fail at some point
#
# 5) Ignore files under data/, meta/ and lib/firmware, as they could be
#    any ELF binaries for non-Fuchsia systems, or for testing.
#
# 6) Verify that debug (unstripped) files for binaries are provided
#    in the build root directory, or toolchain-specific lib directories.
#    Missing entries just generate a WARNING, but are not an error
#    because they do happen in practice (e.g. for prebuilt driver
#    binaries), though are quite rare.
#


def rewrite_elf_needed(dep: str) -> Optional[str]:
    """Rewrite a DT_NEEDED dependency name.

  Args:
    dep: dependency name as it appears in ELF DT_NEEDED entry (e.g. 'libc.so')
  Returns:
    None if the dependency should be ignored, or the input dependency name,
    possibly rewritten for specific cases (e.g. 'libc.so' -> 'ld.so.1')
  """
    if dep == 'libzircon.so':
        # libzircon.so being injected by the kernel into user processes, it should
        # not appear in Fuchsia packages, and thus should be ignored.
        return None
    if dep == 'libc.so':
        # ld.so.1 acts as both the dynamic loader and C library, so any reference
        # to libc.so should be rewritten as 'ld.so.1'
        return 'ld.so.1'

    # For all other cases, just return the unmodified dependency name.
    return dep


def verify_elf_dependencies(
        binary: str, lib_dir: str, deps: Iterable[str],
        elf_entries: Dict[str, Any]) -> Tuple[List[str], List[str]]:
    """Verify the ELF dependencies of a given ELF binary.

  Args:
    binary: Name of the binary whose dependencies are verified.
    lib_dir: The target directory where all dependencies should be.
    deps: An iteration of dependency names, as they appear in DT_NEEDED
      entries.
    elf_entries: The global {target_path: elf_info} map.
  Returns:
    An (errors, libraries) tuple, where 'errors' is a list of error messages
    in case of failure, or an empty list on succcess, and 'libraries' is a
    list of libraries that were checked by this function.
  """
    # Note that we do allow circular dependencies because they do happen
    # in practice. In particular when generating instrumented binaries,
    # e.g. for the 'asan' case (omitting libzircon.so):
    #
    #     libc.so (a.k.a. ld.so.1)
    #       ^     ^         ^ |
    #       |     |         | v
    #       |     |    libclang_rt.asan.so
    #       |     |     | ^      ^
    #       |     |     v |      |
    #       |    libc++abi.so    |
    #       |     |              |
    #       |     v              |
    #     libunwind.so-----------'
    #
    errors = []
    libraries = []
    visited = set()
    queue = list(deps)
    while len(queue) > 0:
        dep = queue[0]
        queue = queue[1:]
        dep2 = rewrite_elf_needed(dep)
        if dep2 is None or dep2 in visited:
            continue
        visited.add(dep2)
        dep_target = os.path.join(lib_dir, dep2)
        info = elf_entries.get(dep_target)
        if not info:
            errors.append('%s missing dependency %s' % (binary, dep_target))
        else:
            libraries.append(dep_target)
            for subdep in info.needed:
                if subdep not in visited:
                    queue.append(subdep)

    return (errors, libraries)


def find_debug_file(
        filename: str,
        depfile_items: Set[str],
        toolchain_lib_dirs: List[str] = []) -> Optional[str]:
    """Find the debug version of a given ELF binary.

        Args:
          filename: input file path in build directory.
          toolchain_lib_dirs: a list of toolchain-specific lib directories
            which will be used to look for debug/.build-id/xx/xxxxxxx.debug
            files corresponding to the input files's build-id value.
          depfile_items: the set of examined files to be updated if needed.

        Returns
          Path to the debug file, if it exists, or None.
        """
    if os.path.exists(filename + '.debug'):
        # Zircon-specific toolchains currently write the debug files
        # for .../foo as .../foo.debug.
        debugfile = filename + '.debug'
    else:
        # Check for toolchain runtime libraries, which are stored under
        # {toolchain}/lib/.../libfoo.so, and whose unstripped file will
        # be under {toolchain}/lib/debug/.build-id/xx/xxxxxx.debug.
        lib_dir = None
        for dir in toolchain_lib_dirs:
            if os.path.realpath(filename).startswith(os.path.realpath(dir) +
                                                     os.sep):
                lib_dir = dir
                break
        if lib_dir:
            build_id_dir = os.path.join(lib_dir, 'debug', '.build-id')
            if not os.path.exists(build_id_dir):
                return None
            build_id = elfinfo.get_elf_info(filename).build_id
            # The build-id value is an hexadecimal string, used to locate the
            # debug file under debug/.build-id/XX/YYYYYY.debug where XX are its
            # first two chars, and YYYYYY is the rest (typically longer than
            # 6 chars).
            debugfile = os.path.join(
                build_id_dir, build_id[:2], build_id[2:] + '.debug')
            if not os.path.exists(debugfile):
                return None
            # Pass filename as fallback so we don't fallback to the build-id entry name.
            return debugfile
        else:
            # Otherwise, the Fuchsia build places unstripped files under
            # .../lib.unstripped/foo.so (for shared library or loadable module
            # .../foo.so) or .../exe.unstripped/bar (for executable .../bar).
            dir, file = os.path.split(filename)
            if file.endswith('.so') or '.so.' in file:
                subdir = 'lib.unstripped'
            else:
                subdir = 'exe.unstripped'
            debugfile = os.path.join(dir, subdir, file)
            while not os.path.exists(debugfile):
                # For dir/foo/bar, if dir/foo/exe.unstripped/bar
                # didn't exist, try dir/exe.unstripped/foo/bar.
                parent, dir = os.path.split(dir)
                if not parent or not dir:
                    return None
                dir, file = parent, os.path.join(dir, file)
                debugfile = os.path.join(dir, subdir, file)
            if not os.path.exists(debugfile):
                debugfile = os.path.join(subdir, filename)
                if not os.path.exists(debugfile):
                    return None
    depfile_items.add(debugfile)
    return debugfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--source-dir',
        default='.',
        help='Root directory for source paths. Default is current directory.')
    parser.add_argument(
        '--check-stripped',
        action='store_true',
        help='Verify that ELF binaries are stripped.')
    parser.add_argument(
        '--check-debug-files',
        action='store_true',
        help='Verify that each ELF binary has its own debug file available.' + \
             'this requires --toolchain-lib-dir to find toolchain-provided runtime debug files')
    parser.add_argument(
        '--toolchain-lib-dir',
        default=[],
        action='append',
        metavar='DIR',
        help=
        'Path to toolchain-provided lib directory. Can be used multiple times.')

    group = parser.add_mutually_exclusive_group()
    group.add_argument('--fini-manifest', help='Input FINI manifest.')
    group.add_argument(
        '--partial-manifest', help='Input partial distribution manifest.')

    parser.add_argument('--stamp', required=True, help='Output stamp file.')
    parser.add_argument('--depfile', help='Output Ninja depfile.')

    args = parser.parse_args()

    depfile_items = set()

    # Read the input manifest into a {target: source} dictionary.
    manifest_entries = {}
    elf_runtime_dir_map = None

    if args.fini_manifest:
        input_manifest = args.fini_manifest
        with open(args.fini_manifest) as f:
            for line in f:
                line = line.rstrip()
                target, _, source = line.partition('=')
                assert source is not None, (
                    'Invalid manifest line: [%s]' % line)

                source_file = os.path.join(args.source_dir, source)

                # Duplicate entries happen in some manifests, but they will have the
                # same content, so assert otherwise.
                if target in manifest_entries:
                    assert manifest_entries[target] == source_file, (
                        'Duplicate entries for target "%s": %s vs %s' %
                        (target, source_file, manifest_entries[target]))

                assert os.path.exists(source_file), (
                    'Missing source file for manifest line: %s' % line)
                manifest_entries[target] = source_file
    else:
        input = args.input_manifest
        with open(args.partial_manifest) as f:
            partial_entries = json.load(f)

        result = distribution_manifest.expand_partial_manifest_items(
            partial_entries, depfile_items)
        if result.errors:
            print(
                'ERRORS FOUND IN %s:\n%s' %
                (args.partial_manifest, '\n'.join(result.errors)),
                file=sys.stderr)

        manifest_entries = {e.destination: e.source for e in result.entries}
        elf_runtime_dir_map = result.elf_runtime_map

    # Filter the input manifest entries to keep only the ELF ones
    # that are not under data/, lib/firmware/ or meta/
    elf_entries = {}
    for target, source_file in manifest_entries.items():
        if target.startswith('data/') or target.startswith(
                'lib/firmware/') or target.startswith('meta/'):
            continue

        depfile_items.add(source_file)
        info = elfinfo.get_elf_info(source_file)
        if info is not None:
            elf_entries[target] = info

    # errors contains a list of error strings corresponding to issues found in
    # the input manifest.
    #
    # extras contains non-error strings that are useful to dump when an error
    # occurs, and give more information about what was found in the manifest.
    # These should only be printed in case of errors, or ignored otherwise.
    errors = []
    extras = []

    # The set of all libraries visited when checking dependencies.
    visited_libraries = set()

    # Verify that libzircon.so or libc.so do not appear inside the package.
    for target, info in elf_entries.items():
        filename = os.path.basename(target)
        if filename in ('libzircon.so', 'libc.so'):
            errors.append(
                '%s should not be in this package (source=%s)!' %
                (target, info.filename))

    # First verify all executables, since we can find their library directory
    # from their PT_INTERP value, then check their dependencies recursively.
    for target, info in elf_entries.items():

        if elf_runtime_dir_map is not None:
            lib_dir = elf_runtime_dir_map.get(target)
            if not lib_dir:
                continue
        else:
            interp = info.interp
            if interp is None:
                continue

            interp_name = os.path.basename(interp)
            if interp_name != 'ld.so.1':
                errors.append(
                    '%s has invalid or unsupported PT_INTERP value: %s' %
                    (target, interp))
                continue

            lib_dir = os.path.join('lib', os.path.dirname(interp))
            extras.append(
                'Binary %s has interp %s, lib_dir %s' %
                (target, interp, lib_dir))

        binary_errors, binary_deps = verify_elf_dependencies(
            target, lib_dir, info.needed, elf_entries)

        errors += binary_errors
        visited_libraries.update(binary_deps)

    # Check that all binaries are stripped if necessary.
    if args.check_stripped:
        for target, info in elf_entries.items():
            if not info.stripped:
                errors.append(
                    '%s is not stripped: %s' % (target, info.filename))

    # Verify that all ELF files have their debug file available.
    # This is only a WARNING, not an error, since it can happen for
    # prebuilt drivers, or with Go binaries.
    if args.check_debug_files:
        for target, source_file in manifest_entries.items():
            if target in elf_entries:
                debugfile = find_debug_file(
                    source_file, depfile_items, args.toolchain_lib_dir)
                if debugfile is None:
                    print('WARNING: No debug file found for %s' % source_file)

    if errors:
        print(
            'ERRORS FOUND IN %s:\n%s' % (input_manifest, '\n'.join(errors)),
            file=sys.stderr)
        if extras:
            print('\n'.join(extras), file=sys.stderr)
        return 1

    if args.depfile:
        with open(args.depfile, 'w') as f:
            f.write(
                '%s: %s\n' % (input_manifest, ' '.join(sorted(depfile_items))))

    # Write the stamp file on success.
    with open(args.stamp, 'w') as f:
        f.write('')

    return 0


if __name__ == "__main__":
    sys.exit(main())
