#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import contextlib
import errno
import json
import os
import shutil
import sys
import tarfile
import tempfile


@contextlib.contextmanager
def _open_archive(archive, directory):
    '''Manages a directory in which an existing SDK is laid out.'''
    if directory:
        yield directory
    elif archive:
        temp_dir = tempfile.mkdtemp(prefix='fuchsia-merger')
        # Extract the tarball into the temporary directory.
        # This is vastly more efficient than accessing files one by one via
        # the tarfile API.
        with tarfile.open(archive) as archive_file:
            archive_file.extractall(temp_dir)
        try:
            yield temp_dir
        finally:
            shutil.rmtree(temp_dir, ignore_errors=True)
    else:
        raise Exception('Error: archive or directory must be set')


@contextlib.contextmanager
def _open_output(archive, directory):
    '''Manages the output of this script.'''
    if directory:
        # Remove any existing output.
        shutil.rmtree(directory, ignore_errors=True)
        yield directory
    elif archive:
        temp_dir = tempfile.mkdtemp(prefix='fuchsia-merger')
        try:
            yield temp_dir
            # Write the archive file.
            with tarfile.open(archive, "w:gz") as archive_file:
                archive_file.add(temp_dir, arcname='')
        finally:
            shutil.rmtree(temp_dir, ignore_errors=True)
    else:
        raise Exception('Error: archive or directory must be set')


def _get_manifest(sdk_dir):
    '''Returns the set of elements in the given SDK.'''
    with open(os.path.join(sdk_dir, 'meta', 'manifest.json'), 'r') as manifest:
        return json.load(manifest)


def _get_meta(element, sdk_dir):
    '''Returns the contents of the given element's manifest in a given SDK.'''
    with open(os.path.join(sdk_dir, element), 'r') as meta:
        return json.load(meta)


def _get_files(element_meta):
    '''Extracts the files associated with the given element.
    Returns a 2-tuple containing:
     - the set of arch-independent files;
     - the sets of arch-dependent files, indexed by architecture.
    '''
    type = element_meta['type']
    common_files = set()
    arch_files = {}
    if type == 'cc_prebuilt_library':
        common_files.update(element_meta['headers'])
        for arch, binaries in element_meta['binaries'].iteritems():
            contents = set()
            contents.add(binaries['link'])
            if 'dist' in binaries:
                contents.add(binaries['dist'])
            if 'debug' in binaries:
                contents.add(binaries['debug'])
            arch_files[arch] = contents
    elif type == 'cc_source_library':
        common_files.update(element_meta['headers'])
        common_files.update(element_meta['sources'])
    elif type == 'dart_library':
        common_files.update(element_meta['sources'])
    elif type == 'fidl_library':
        common_files.update(element_meta['sources'])
    elif type == 'host_tool':
        common_files.update(element_meta['files'])
    elif type == 'image':
        for arch, file in element_meta['file'].iteritems():
            arch_files[arch] = set([file])
    elif type == 'loadable_module':
        common_files.update(element_meta['resources'])
        arch_files.update(element_meta['binaries'])
    elif type == 'sysroot':
        for arch, version in element_meta['versions'].iteritems():
            contents = set()
            contents.update(version['headers'])
            contents.update(version['link_libs'])
            contents.update(version['dist_libs'])
            contents.update(version['debug_libs'])
            arch_files[arch] = contents
    else:
        raise Exception('Unknown element type: ' + type)
    return (common_files, arch_files)


def _ensure_directory(path):
    '''Ensures that the directory hierarchy of the given path exists.'''
    target_dir = os.path.dirname(path)
    try:
        os.makedirs(target_dir)
    except OSError as exception:
        if exception.errno == errno.EEXIST and os.path.isdir(target_dir):
            pass
        else:
            raise


def _copy_file(file, source_dir, dest_dir):
    '''Copies a file to a given path, taking care of creating directories if
    needed.
    '''
    source = os.path.join(source_dir, file)
    destination = os.path.join(dest_dir, file)
    _ensure_directory(destination)
    shutil.copy2(source, destination)


def _copy_files(files, source_dir, dest_dir):
    '''Copies a set of files to a given directory.'''
    for file in files:
        _copy_file(file, source_dir, dest_dir)


def _copy_identical_files(set_one, source_dir_one, set_two, source_dir_two,
                          dest_dir):
    '''Verifies that two sets of files are absolutely identical and then copies
    them to the output directory.
    '''
    if set_one != set_two:
        return False
    # Not verifying that the contents of the files are the same, as builds are
    # not exactly stable at the moment.
    _copy_files(set_one, source_dir_one, dest_dir)
    return True


def _copy_element(element, source_dir, dest_dir):
    '''Copy an entire SDK element to a given directory.'''
    meta = _get_meta(element, source_dir)
    common_files, arch_files = _get_files(meta)
    files = common_files
    for more_files in arch_files.itervalues():
        files.update(more_files)
    _copy_files(files, source_dir, dest_dir)
    # Copy the metadata file as well.
    _copy_file(element, source_dir, dest_dir)


def _write_meta(element, source_dir_one, source_dir_two, dest_dir):
    '''Writes a meta file for the given element, resulting from the merge of the
    meta files for that element in the two given SDK directories.
    '''
    meta_one = _get_meta(element, source_dir_one)
    meta_two = _get_meta(element, source_dir_two)
    # TODO(DX-340): verify that the common parts of the metadata files are in
    # fact identical.
    type = meta_one['type']
    meta = {}
    if type == 'cc_prebuilt_library' or type == 'loadable_module':
        meta = meta_one
        meta['binaries'].update(meta_two['binaries'])
    elif type == 'image':
        meta = meta_one
        meta['file'].update(meta_two['file'])
    elif type == 'sysroot':
        meta = meta_one
        meta['versions'].update(meta_two['versions'])
    elif (type == 'cc_source_library' or type == 'dart_library' or
          type == 'fidl_library' or type == 'host_tool'):
        # These elements are arch-independent, the metadata does not need any
        # update.
        meta = meta_one
    else:
        raise Exception('Unknown element type: ' + type)
    meta_path = os.path.join(dest_dir, element)
    _ensure_directory(meta_path)
    with open(meta_path, 'w') as meta_file:
        json.dump(meta, meta_file, indent=2, sort_keys=True)
    return True


def _write_manifest(source_dir_one, source_dir_two, dest_dir):
    '''Writes a manifest file resulting from the merge of the manifest files for
    the two given SDK directories.
    '''
    manifest_one = _get_manifest(source_dir_one)
    manifest_two = _get_manifest(source_dir_two)

    # Host architecture.
    if manifest_one['arch']['host'] != manifest_two['arch']['host']:
        print('Error: mismatching host architecture')
        return False
    manifest = {
        'arch': {
            'host': manifest_one['arch']['host'],
        }
    }

    # Target architectures.
    manifest['arch']['target'] = sorted(set(manifest_one['arch']['target']) |
                                        set(manifest_two['arch']['target']))

    # Parts.
    manifest['parts'] = sorted(set(manifest_one['parts']) |
                               set(manifest_two['parts']))

    manifest_path = os.path.join(dest_dir, 'meta', 'manifest.json')
    _ensure_directory(manifest_path)
    with open(manifest_path, 'w') as manifest_file:
        json.dump(manifest, manifest_file, indent=2, sort_keys=True)
    return True


def main():
    parser = argparse.ArgumentParser(
            description=('Merges the contents of two SDKs'))
    alpha_group = parser.add_mutually_exclusive_group(required=True)
    alpha_group.add_argument('--alpha-archive',
                             help='Path to the first SDK - as an archive',
                             default='')
    alpha_group.add_argument('--alpha-directory',
                             help='Path to the first SDK - as a directory',
                             default='')
    beta_group = parser.add_mutually_exclusive_group(required=True)
    beta_group.add_argument('--beta-archive',
                            help='Path to the second SDK - as an archive',
                            default='')
    beta_group.add_argument('--beta-directory',
                            help='Path to the second SDK - as a directory',
                            default='')
    output_group = parser.add_mutually_exclusive_group(required=True)
    output_group.add_argument('--output-archive',
                              help='Path to the merged SDK - as an archive',
                              default='')
    output_group.add_argument('--output-directory',
                              help='Path to the merged SDK - as a directory',
                              default='')
    args = parser.parse_args()

    has_errors = False

    with _open_archive(args.alpha_archive, args.alpha_directory) as alpha_dir, \
         _open_archive(args.beta_archive, args.beta_directory) as beta_dir, \
         _open_output(args.output_archive, args.output_directory) as out_dir:

        alpha_elements = set(_get_manifest(alpha_dir)['parts'])
        beta_elements = set(_get_manifest(beta_dir)['parts'])
        common_elements = alpha_elements & beta_elements

        # Copy elements that appear in a single SDK.
        for element in sorted(alpha_elements - common_elements):
            _copy_element(element, alpha_dir, out_dir)
        for element in (beta_elements - common_elements):
            _copy_element(element, beta_dir, out_dir)

        # Verify and merge elements which are common to both SDKs.
        for element in sorted(common_elements):
            alpha_meta = _get_meta(element, alpha_dir)
            beta_meta = _get_meta(element, beta_dir)
            alpha_common, alpha_arch = _get_files(alpha_meta)
            beta_common, beta_arch = _get_files(beta_meta)

            # Common files should not vary.
            if not _copy_identical_files(alpha_common, alpha_dir, beta_common,
                                         beta_dir, out_dir):
                print('Error: different common files for ' + element)
                has_errors = True
                continue

            # Arch-dependent files need to be merged in the metadata.
            all_arches = set(alpha_arch.keys()) | set(beta_arch.keys())
            for arch in all_arches:
                if arch in alpha_arch and arch in beta_arch:
                    if not _copy_identical_files(alpha_arch[arch], alpha_dir,
                                                 beta_arch[arch], beta_dir,
                                                 out_dir):
                        print('Error: different %s files for %s' % (arch,
                                                                   element))
                        has_errors = True
                        continue
                elif arch in alpha_arch:
                    _copy_files(alpha_arch[arch], alpha_dir, out_dir)
                elif arch in beta_arch:
                    _copy_files(beta_arch[arch], beta_dir, out_dir)

            if not _write_meta(element, alpha_dir, beta_dir, out_dir):
                print('Error: unable to merge meta for ' + element)
                has_errors = True

        if not _write_manifest(alpha_dir, beta_dir, out_dir):
            print('Error: could not write manifest file')
            has_errors = True

        # TODO(DX-340): verify that metadata files are valid.

    return 1 if has_errors else 0


if __name__ == '__main__':
    sys.exit(main())
