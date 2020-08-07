#!/usr/bin/env python3.8
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
This tool takes in multiple manifest files:
 * system image and archive manifest files from each package
 * Zircon's bootfs.manifest, optionally using a subset selected by the
   "group" syntax (e.g. could specify just "core", or "core,misc" or
   "core,misc,test").
 * "auxiliary" manifests
 ** one from the toolchain for the target libraries (libc++ et al)
 ** one from the Zircon/*-ulib build, which has the Zircon ASan libraries
 ** the unselected parts of the "main" manifests (i.e. Zircon)

It emits final /boot and /system manifests used to make the actual images,
final archive manifests used to make each package, and the build ID map.

The "auxiliary" manifests just supply a pool of files that might be used to
satisfy dependencies; their files are not included in the output a priori.

The tool examines each file in its main input manifests.  If it's not an
ELF file, it just goes into the appropriate output manifest.  If it's an
ELF file, then the tool figures out what "variant" it is (if any), such as
"asan" and what other ELF files it requires via PT_INTERP and DT_NEEDED.
It then finds those dependencies and includes them in the output manifest,
and iterates on their dependencies.  Each dependency is found either in the
*-shared/ toolchain $root_out_dir for the same variant toolchain that built
the root file, or among the files in auxiliary manifests (i.e. toolchain
and Zircon libraries).  For things built in the asan variant, it finds the
asan versions of the toolchain/Zircon libraries.
"""

from collections import namedtuple
import argparse
import elfinfo
import fnmatch
import itertools
import manifest
import os
import sys
import variant

binary_info = variant.binary_info

# An entry for a binary is (manifest.manifest_entry, elfinfo.elf_info).
binary_entry = namedtuple('binary_entry', ['entry', 'info'])

# In recursions of CollectBinaries.AddBinary, this is the type of the
# context argument.
binary_context = namedtuple(
    'binary_context', [
        'variant',
        'soname_map',
        'root_dependent',
    ])

# Each --output argument yields an output_manifest tuple.
output_manifest = namedtuple('output_manifest', ['file', 'manifest'])

# Each --binary argument yields a input_binary tuple.
input_binary = namedtuple('input_binary', ['target_pattern', 'output_group'])


# Collect all the binaries from auxiliary manifests into
# a dictionary mapping entry.target to binary_entry.
def collect_auxiliaries(manifest, examined):
    aux_binaries = {}
    for entry in manifest:
        examined.add(entry.source)
        info = binary_info(entry.source)
        if info:
            new_binary = binary_entry(entry, info)
            binary = aux_binaries.setdefault(entry.target, new_binary)
            if binary.entry.source != new_binary.entry.source:
                raise Exception(
                    "'%s' in both %r and %r" %
                    (entry.target, binary.entry, entry))
    return aux_binaries


# Return an iterable of binary_entry for all the binaries in `manifest` and
# `input_binaries` and their dependencies from `aux_binaries`, and an
# iterable of manifest_entry for all the other files in `manifest`.
def collect_binaries(manifest, input_binaries, aux_binaries, examined):
    # As we go, we'll collect the actual binaries for the output
    # in this dictionary mapping entry.target to binary_entry.
    unexamined_binaries = {}
    binaries = {}

    # We'll collect entries in the manifest that aren't binaries here.
    nonbinaries = []

    # This maps GN toolchain (from variant.shared_toolchain) to a
    # dictionary mapping DT_SONAME string to binary_entry.
    soname_map_by_toolchain = {}

    def rewrite_binary_group(old_binary, group_override):
        return binary_entry(
            old_binary.entry._replace(group=group_override), old_binary.info)

    def add_binary(binary, context=None, auxiliary=False):
        # Add a binary by target name.
        def add_auxiliary(target, required, group_override=None):
            if group_override is None:
                group_override = binary.entry.group
                aux_context = context
            else:
                aux_context = None
            # Look for the target in auxiliary manifests.
            aux_binary = aux_binaries.get(target)
            if required:
                assert aux_binary, (
                    "'%s' not in auxiliary manifests, needed by %r via %r" %
                    (target, binary.entry, context.root_dependent))
            if aux_binary:
                add_binary(
                    rewrite_binary_group(aux_binary, group_override),
                    aux_context, True)
                return True
            return False

        existing_binary = binaries.get(binary.entry.target)
        if existing_binary is not None:
            if existing_binary.entry.source != binary.entry.source:
                raise Exception(
                    "%r in both %r and %r" %
                    (binary.entry.target, existing_binary, binary))
            # If the old record was in a later group, we still need to
            # process all the dependencies again to promote them to
            # the new group too.
            if existing_binary.entry.group <= binary.entry.group:
                return

        examined.add(binary.entry.source)

        # If we're not part of a recursion, discover the binary's context.
        if context is None:
            binary_variant, variant_file = variant.find_variant(
                binary.info, binary.entry.target)
            if variant_file is not None:
                # This is a variant that was actually built in a different
                # place than its original name says.  Rewrite everything to
                # refer to the "real" name.
                binary = binary_entry(
                    binary.entry._replace(source=variant_file),
                    binary.info.rename(variant_file))
                examined.add(variant_file)
            context = binary_context(
                binary_variant,
                soname_map_by_toolchain.setdefault(
                    binary_variant.shared_toolchain, {}), binary)

        binaries[binary.entry.target] = binary
        assert binary.entry.group is not None, binary

        if binary.info.soname:
            # This binary has a SONAME, so record it in the map.
            soname_binary = context.soname_map.setdefault(
                binary.info.soname, binary)
            if soname_binary.entry.source != binary.entry.source:
                raise Exception(
                    "SONAME '%s' in both %r and %r" %
                    (binary.info.soname, soname_binary, binary))
            if binary.entry.group < soname_binary.entry.group:
                # Update the record to the earliest group.
                context.soname_map[binary.info.soname] = binary

        # The PT_INTERP is implicitly required from an auxiliary manifest.
        if binary.info.interp:
            add_auxiliary('lib/' + binary.info.interp, True)

        # The variant might require other auxiliary binaries too.
        for variant_aux, variant_aux_group in context.variant.aux:
            add_auxiliary(variant_aux, True, variant_aux_group)

        # Handle the DT_NEEDED list.
        for soname in binary.info.needed:
            # The vDSO is not actually a file.
            if soname == 'libzircon.so':
                continue

            lib = context.soname_map.get(soname)
            if lib and lib.entry.group <= binary.entry.group:
                # Already handled this one in the same or earlier group.
                continue

            # The DT_SONAME is libc.so, but the file is ld.so.1 on disk.
            if soname == 'libc.so':
                soname = 'ld.so.1'

            # Translate the SONAME to a target file name.
            target = context.variant.soname_target(soname)
            if add_auxiliary(target, False):
                # We found it in an existing manifest.
                continue

            # Check if it's elsewhere in the input set.
            lib = unexamined_binaries.get(target)
            if lib is None:
                # It must be in the shared_toolchain output directory.
                shared_toolchain = context.variant.shared_toolchain

                # TODO(38226): See //sdk/lib/fdio/BUILD.gn.
                # libFuzzer depends on libfdio, so fuzzers need to use a
                # version of fdio without SanitizerCoverage instrumentation to
                # avoid polluting coverage data for the code under test.
                if soname == 'libfdio.so' and shared_toolchain.endswith(
                        '-fuzzer-shared'):
                    shared_toolchain = shared_toolchain[:-len('-fuzzer-shared')]
                    shared_toolchain += '-shared'

                # Context like group is inherited from the dependent.
                lib_entry = binary.entry._replace(
                    source=os.path.join(shared_toolchain, soname),
                    target=target)

                assert os.path.exists(lib_entry.source), (
                    "missing %r needed by %r via %r" %
                    (lib_entry, binary, context.root_dependent))

                # Read its ELF info and sanity-check.
                lib = binary_entry(lib_entry, binary_info(lib_entry.source))

            assert lib.info and lib.info.soname == soname, (
                "SONAME '%s' expected in %r, needed by %r via %r" %
                (soname, lib, binary, context.root_dependent))

            # Recurse.
            add_binary(lib, context)

    for entry in manifest:
        info = None
        # Don't inspect data or firmware resources in the manifest.  Regardless
        # of the bits in these files, we treat them as opaque data.
        try:
            if not entry.target.startswith(
                    'data/') and not entry.target.startswith('lib/firmware/'):
                info = binary_info(entry.source)
        except IOError as e:
            raise Exception('%s from %s' % (e, entry))
        if info:
            if (entry.target not in unexamined_binaries or entry.group <
                    unexamined_binaries[entry.target].entry.group):
                unexamined_binaries[entry.target] = binary_entry(entry, info)
        else:
            nonbinaries.append(entry)

    for binary in unexamined_binaries.values():
        add_binary(binary)
    for target in unexamined_binaries.keys():
        assert target in binaries, (
            "Target %s missing from %s" % (target, binaries.keys()))

    matched_binaries = set()
    for input_binary in input_binaries:
        matches = fnmatch.filter(
            iter(aux_binaries.keys()), input_binary.target_pattern)
        assert matches, (
            "--input-binary='%s' did not match any binaries" %
            input_binary.target_pattern)
        for target in matches:
            assert target not in matched_binaries, (
                "'%s' matched by multiple --input-binary patterns" % target)
            matched_binaries.add(target)
            add_binary(
                rewrite_binary_group(
                    aux_binaries[target], input_binary.output_group),
                auxiliary=True)

    return iter(binaries.values()), nonbinaries


# Take an iterable of binary_entry, and return list of binary_entry (all
# stripped files), a list of binary_info (all debug files), and a boolean
# saying whether any new stripped output files were written in the process.
def strip_binary_manifest(
        manifest, stripped_dir, build_id_dir, toolchain_lib_dirs, examined):
    new_output = False

    def find_debug_file(filename):
        # In the Zircon makefile build, the file to be installed is called
        # foo.strip and the unstripped file is called foo.  In the new Zircon
        # GN build, the file to be installed is called foo and the unstripped
        # file is called foo.debug.  In the Fuchsia GN build, the file to be
        # installed is called foo and the unstripped file has the same name in
        # the exe.unstripped or lib.unstripped subdirectory.
        if filename.endswith('.strip'):
            debugfile = filename[:-6]
        elif os.path.exists(filename + '.debug'):
            debugfile = filename + '.debug'
        elif (lib_dir := next((dir for dir in toolchain_lib_dirs if
                os.path.realpath(filename).startswith(os.path.realpath(dir) + os.sep)),
                None)):
            build_id_dir = os.path.join(lib_dir, 'debug', '.build-id')
            if not os.path.exists(build_id_dir):
                return None
            info = elfinfo.get_elf_info(filename)
            debugfile = os.path.join(
                build_id_dir, info.build_id[:2], info.build_id[2:] + '.debug')
            if not os.path.exists(debugfile):
                return None
            # Pass filename as fallback so we don't fallback to the build-id entry name.
            return binary_info(debugfile, fallback_soname=os.path.basename(filename))
        else:
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
        debug = binary_info(debugfile)
        assert debug, (
            "Debug file '%s' for '%s' is invalid" % (debugfile, filename))
        examined.add(debugfile)
        return debug

    # The toolchain-supplied shared libraries are delivered unstripped.  For
    # these, strip the binary right here and update the manifest entry to point
    # to the stripped file.
    def make_debug_file(entry, info):
        debug = info
        stripped = os.path.join(stripped_dir, entry.target)
        dir = os.path.dirname(stripped)
        if not os.path.isdir(dir):
            os.makedirs(dir)
        if info.strip(stripped):
            new_output = True
        info = binary_info(stripped)
        assert info, (
            "Stripped file '%s' for '%s' is invalid" %
            (stripped, debug.filename))
        examined.add(debug.filename)
        examined.add(stripped)
        return entry._replace(source=stripped), info, debug

    stripped_manifest = []
    debug_list = []
    for entry, info in manifest:
        assert entry.source == info.filename
        if info.stripped:
            debug = find_debug_file(info.filename)
        else:
            entry, info, debug = make_debug_file(entry, info)
        stripped_manifest.append(binary_entry(entry, info))
        if debug is None:
            print('WARNING: no debug file found for %s' % info.filename)
            continue
        assert not debug.stripped, "'%s' is stripped" % debug.filename
        assert info == debug._replace(
            filename=info.filename, sizes=info.sizes,
            stripped=True), ("Debug file mismatch: %r vs %r" % (info, debug))
        if debug.build_id:
            debug_list.append(debug)
        else:
            # Every binary should have a build ID, except for test cases
            # specifically testing missing-build-ID or missing-PT_NOTE cases.
            # Those will have 'test' in the name.
            assert 'test' in os.path.basename(
                debug.filename), ("'%s' has no build ID" % debug.filename)

    return stripped_manifest, debug_list, new_output


def emit_manifests(args, selected, unselected, input_binaries):

    def update_file(file, contents, force=False):
        if (not force and os.path.exists(file) and
                os.path.getsize(file) == len(contents)):
            with open(file, 'r') as f:
                if f.read() == contents:
                    return
        with open(file, 'w') as f:
            f.write(contents)

    # The name of every file we examine to make decisions goes into this set.
    examined = set(args.manifest)

    # Collect all the inputs and reify.
    aux_binaries = collect_auxiliaries(unselected, examined)
    binaries, nonbinaries = collect_binaries(
        selected, input_binaries, aux_binaries, examined)

    # Prepare to collate groups.
    outputs = [output_manifest(file, []) for file in args.output]

    # Finalize the output binaries.  If stripping wrote any new/changed files,
    # then force an update of the manifest file even if it's identical.  The
    # manifest file's timestamp is what GN/Ninja sees as running this script
    # having touched any of its outputs, and GN/Ninja doesn't know that the
    # stripped files are implicit outputs (there's no such thing as a depfile
    # for outputs, only for inputs).
    binaries, debug_files, force_update = strip_binary_manifest(
        binaries, args.stripped_dir, args.build_id_dir, args.toolchain_lib_dir,
        examined)

    # Collate groups.
    for entry in itertools.chain((binary.entry for binary in binaries),
                                 nonbinaries):
        outputs[entry.group].manifest.append(entry._replace(group=None))

    all_binaries = {binary.info.build_id: binary.entry for binary in binaries}
    all_debug_files = {info.build_id: info for info in debug_files}

    # Emit each primary manifest.
    for output in outputs:
        depfile_output = output.file
        # Sort so that functionally identical output is textually
        # identical.
        output.manifest.sort(key=lambda entry: entry.target)
        update_file(
            output.file, manifest.format_manifest_file(output.manifest),
            force_update)

    # Emit the depfile.
    if args.depfile:
        with open(args.depfile, 'w') as f:
            f.write(depfile_output + ':')
            for file in sorted(examined):
                f.write(' ' + file)
            f.write('\n')


class input_binary_action(argparse.Action):

    def __call__(self, parser, namespace, values, option_string=None):
        binaries = getattr(namespace, self.dest, None)
        if binaries is None:
            binaries = []
            setattr(namespace, self.dest, binaries)
        outputs = getattr(namespace, 'output', None)
        output_group = len(outputs) - 1
        binaries.append(input_binary(values, output_group))


def parse_args():
    parser = argparse.ArgumentParser(
        description='''
Massage manifest files from the build to produce images.
''',
        epilog='''
The --cwd and --group options apply to subsequent --manifest arguments.
Each input --manifest is assigned to the preceding --output argument file.
Any input --manifest that precedes all --output arguments
just supplies auxiliary files implicitly required by other (later) input
manifests, but does not add all its files to any --output manifest.  This
is used for shared libraries and the like.
''',
        fromfile_prefix_chars='@')
    parser.add_argument(
        '--build-id-dir',
        required=False,
        metavar='DIR',
        help='.build-id directory to populate when stripping')
    parser.add_argument(
        '--toolchain-lib-dir',
        default=[],
        action='append',
        metavar='DIR',
        help='Path to a toolchain library directory (multiple allowed)')
    parser.add_argument(
        '--depfile', metavar='DEPFILE', help='Ninja depfile to write')
    parser.add_argument(
        '--binary',
        action=input_binary_action,
        default=[],
        metavar='PATH',
        help='Take matching binaries from auxiliary manifests')
    parser.add_argument(
        '--stripped-dir',
        required=True,
        metavar='STRIPPED_DIR',
        help='Directory to hold stripped copies when needed')
    return manifest.common_parse_args(parser)


def main():
    args = parse_args()
    emit_manifests(args, args.selected, args.unselected, args.binary)


if __name__ == "__main__":
    main()
