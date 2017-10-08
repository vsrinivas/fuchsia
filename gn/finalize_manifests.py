#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This tool takes in multiple manifest files:
 * /boot and /system manifests from each package
 * Zircon's bootfs.manifest, optionally using a subset selected by the
   "group" syntax (e.g. could specify just "core", or "core,misc" or
   "core,misc,test").
 * "auxiliary" manifests
 ** one from the toolchain for the target libraries (libc++ et al)
 ** one from the build-zircon/*-ulib build, which has the Zircon ASan libraries
 ** the unselected parts of the "main" manifests (i.e. Zircon)

It emits final /boot and /system manifests used to make the actual images,
and the build ID map.

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
and Zircon libraries).  For things built in the asan variant, it finds the asan versions of the toolchain/Zircon libraries.
"""

from collections import namedtuple
import argparse
import itertools
import manifest
import os
import variant


binary_info = variant.binary_info

# An entry for a binary is (manifest.manifest_entry, elfinfo.elf_info).
binary_entry = namedtuple('binary_entry', ['entry', 'info'])

# In recursions of CollectBinaries.AddBinary, this is the type of the
# context argument.
binary_context = namedtuple('binary_context', [
    'variant',
    'soname_map',
    'root_dependent',
])

# Each --manifest argument yields an input_manifest tuple.
input_manifest = namedtuple('input_manifest', [
    'file',
    'cwd',
    'groups',
    'output_group',
])

# Each --output argument yields an output_manifest tuple.
output_manifest = namedtuple('output_manifest', ['file', 'manifest'])


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
                    (entry.target, binary.entry.manifest, entry.manifest))
    return aux_binaries


# Return an iterable of binary_entry for all the binaries in `manifest` and
# their dependencies from `aux_binaries`, and an iterable of manifest_entry
# for all the other files in `manifest`.
def collect_binaries(manifest, aux_binaries, examined):
    # As we go, we'll collect the actual binaries for the output
    # in this dictionary mapping entry.target to binary_entry.
    binaries = {}

    # We'll collect entries in the manifest that aren't binaries here.
    nonbinaries = []

    # This maps GN toolchain (from variant.shared_toolchain) to a
    # dictionary mapping DT_SONAME string to binary_entry.
    soname_map_by_toolchain = {}

    def add_binary(binary, context=None, auxiliary=False):
        def RewriteBinaryGroup(old_binary, group_override):
            return binary_entry(
                old_binary.entry._replace(group=group_override),
                old_binary.info)

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
                add_binary(RewriteBinaryGroup(aux_binary, group_override),
                           aux_context, True)
                return True
            return False

        existing_binary = binaries.get(binary.entry.target)
        if existing_binary is not None:
            if existing_binary.entry.source != binary.entry.source:
                raise Exception("%r in both %r and %r" %
                                (binary.entry.target, existing_binary, binary))
            # If the old record was in a later group, we still need to
            # process all the dependencies again to promote them to
            # the new group too.
            if existing_binary.entry.group <= binary.entry.group:
                return

        examined.add(binary.entry.source)

        # If we're not part of a recursion, discover the binary's context.
        if context is None:
            binary_variant, variant_file = variant.find_variant(binary.info)
            if variant_file is not None:
              # This is a variant that was actually built in a different
              # place than its original name says.  Rewrite everything to
              # refer to the "real" name.
              binary = binary_entry(binary.entry._replace(source=variant_file),
                                    binary.info.rename(variant_file))
              examined.add(variant_file)
            context = binary_context(binary_variant,
                                     soname_map_by_toolchain.setdefault(
                                         binary_variant.shared_toolchain, {}),
                                     binary)

        binaries[binary.entry.target] = binary
        assert binary.entry.group is not None

        if binary.info.soname:
            # This binary has a SONAME, so record it in the map.
            soname_binary = context.soname_map.setdefault(binary.info.soname,
                                                          binary)
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
            target = ('lib/' +
                      ('' if soname == context.variant.runtime
                       else context.variant.libprefix) +
                      soname)
            if add_auxiliary(target, auxiliary):
                # We found it in an existing manifest.
                continue

            # An auxiliary's dependencies must all be auxiliaries too.
            assert not auxiliary, (
                "missing '%s' needed by auxiliary %r via %r" %
                 (target, binary, context.root_dependent))

            # It must be in the shared_toolchain output directory.
            # Context like group is inherited from the dependent.
            lib_entry = binary.entry._replace(
                source=os.path.join(context.variant.shared_toolchain, soname),
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
        try:
            info = binary_info(entry.source)
        except IOError as e:
            raise Exception('%s from %s' % (e, entry))
        if info:
            add_binary(binary_entry(entry, info))
        else:
            nonbinaries.append(entry)

    return binaries.itervalues(), nonbinaries


# Take an iterable of binary_entry, and return list of manifest_entry (all
# stripped files) and a list of binary_info (all debug files).
def strip_binary_manifest(manifest, stripped_dir, examined):
    def file_older(output, input):
        return (not os.path.exists(output) or
                os.path.getmtime(output) < os.path.getmtime(input))

    def find_debug_file(filename):
        # In the Zircon makefile build, the file to be installed is called
        # foo.strip and the unstripped file is called foo.  In the GN build,
        # the file to be installed is called foo and the unstripped file has
        # the same name in the exe.unstripped or lib.unstripped subdirectory.
        if filename.endswith('.strip'):
            debugfile = filename[:-6]
        else:
            dir, file = os.path.split(filename)
            if file.endswith('.so') or '.so.' in file:
                subdir = 'lib.unstripped'
            else:
                subdir = 'exe.unstripped'
            debugfile = os.path.join(dir, subdir, file)
            if not os.path.exists(debugfile):
                debugfile = os.path.join(subdir, filename)
                if not os.path.exists(debugfile):
                    return None
        debug = binary_info(debugfile)
        assert debug, ("Debug file '%s' for '%s' is invalid" %
                       (debugfile, filename))
        examined.add(debugfile)
        return debug

    # The toolchain-supplied shared libraries, and Go binaries, are
    # delivered unstripped.  For these, strip the binary right here and
    # update the manifest entry to point to the stripped file.
    def make_debug_file(entry, info):
        debug = info
        stripped = os.path.join(stripped_dir, entry.target)
        # If a newer stripped file already exists, just use it.
        if file_older(stripped, debug.filename):
            dir = os.path.dirname(stripped)
            if not os.path.isdir(dir):
                os.makedirs(dir)
            info.strip(stripped)
        info = binary_info(stripped)
        assert info, ("Stripped file '%s' for '%s' is invalid" %
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
        stripped_manifest.append(entry)
        if debug is None:
            print 'WARNING: no debug file found for %s' % info.filename
            continue
        assert debug.build_id, "'%s' has no build ID" % debug.filename
        assert not debug.stripped, "'%s' is stripped" % debug.filename
        assert info == debug._replace(filename=info.filename, stripped=True), (
            "Debug file mismatch: %r vs %r" % (info, debug))
        debug_list.append(debug)

    return stripped_manifest, debug_list


def emit_manifests(outputs, build_id_file, depfile, selected, unselected):
    def update_file(file, contents):
        if os.path.exists(file) and os.path.getsize(file) == len(contents):
            with open(file, 'r') as f:
                if f.read() == contents:
                    return
        with open(file, 'w') as f:
            f.write(contents)

    # The name of every file we examine to make decisions goes into this set.
    examined = set()

    # Collect all the inputs and reify.
    aux_binaries = collect_auxiliaries(unselected, examined)
    binaries, nonbinaries = collect_binaries(selected, aux_binaries, examined)

    # Finalize the output binaries.
    binaries, debug_files = strip_binary_manifest(binaries, 'stripped',
                                                  examined)

    # Collate groups.
    outputs = [output_manifest(file, []) for file in outputs]
    for entry in itertools.chain(binaries, nonbinaries):
        if entry.group is not None:
            outputs[entry.group].manifest.append(entry._replace(group=None))

    # Emit each manifest.
    # Sort so that functionally identical output is textually identical.
    for output in outputs:
        output.manifest.sort(key=lambda entry: entry.target)
        update_file(output.file, manifest.format_manifest_file(output.manifest))

    # Emit the build ID list.
    # Sort so that functionally identical output is textually identical.
    debug_files.sort(key=lambda info: info.build_id)
    update_file(build_id_file, ''.join(
        info.build_id + ' ' + info.filename + '\n'
        for info in debug_files))

    # Emit the depfile.
    if depfile:
        with open(depfile, 'w') as f:
            f.write(outputs[0].file + ':')
            for file in sorted(examined):
                f.write(' ' + file)
            f.write('\n')


class input_manifest_action(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        inputs = getattr(namespace, self.dest, None)
        if inputs is None:
            inputs = []
            setattr(namespace, self.dest, inputs)
        outputs = getattr(namespace, 'output', None)

        file = values
        groups = namespace.groups
        cwd = getattr(namespace, 'cwd', '')
        output_group = None if outputs is None else len(outputs) - 1

        inputs.append(input_manifest(file, cwd, groups, output_group))


def parse_args():
    parser = argparse.ArgumentParser(description='''
Massage manifest files from the build to produce images.
''',
        epilog='''
The --cwd and --group options apply to subsequent --manifest arguments.
Each input --manifest is assigned to the preceding --output argument file.
Any input --manifest that precedes all --output arguments just supplies
auxiliary files implicitly required by other (later) input manifests, but
does not add all its files to any --output manifest.  This is used for
shared libraries and the like.
''')
    parser.add_argument('--build-id-file', required=True,
                        help='Output build ID list')
    parser.add_argument('--depfile',
                        help='Ninja depfile to write')
    parser.add_argument('--output', action='append', required=True,
                        help='Output manifest file')
    parser.add_argument('--cwd',
                        help='Input entries are relative to this directory')
    parser.add_argument('--groups',
                        help='"all" or comma-separated groups to include')
    parser.add_argument('--manifest', action=input_manifest_action)
    return parser.parse_args()


def main():
    args = parse_args()

    all_selected = []
    all_unselected = []
    for input in args.manifest:
        selected, unselected = manifest.ingest_manifest_file(
            input.file, input.cwd, input.groups, '.', input.output_group)

        all_selected += selected
        all_unselected += unselected

    emit_manifests(args.output, args.build_id_file, args.depfile,
                  all_selected, all_unselected)


if __name__ == "__main__":
    main()
