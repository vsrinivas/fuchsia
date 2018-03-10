#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import errno
import json
import os
import shutil
import sys

FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # sdk
    os.path.abspath(__file__))))

sys.path += [os.path.join(FUCHSIA_ROOT, 'build', 'sdk')]
from sdk_common import Atom


def make_dir(path, is_dir=False):
    '''Creates the directory at the given path.'''
    target = path if is_dir else os.path.dirname(path)
    try:
        os.makedirs(target)
    except OSError as exception:
        if exception.errno == errno.EEXIST and os.path.isdir(target):
            pass
        else:
            raise


def install_cpp_atom(atom, metadata, output):
    '''Installs an atom from the "c-pp" domain.'''
    name = atom.id.name
    type = atom.tags['type']
    if type == 'compiled_shared':
        install_cpp_prebuilt_atom(atom, metadata, output)
    elif type == 'sources':
        install_cpp_source_atom(atom, metadata, output)
    else:
        print('Atom type "%s" not handled, skipping %s.' % (type, name))


def install_cpp_prebuilt_atom(atom, metadata, output):
    '''Installs a prebuilt atom from the "c-pp" domain.'''
    name = atom.id.name
    if atom.tags['arch'] != 'target':
        print('Only libraries compiled for a target are supported, '
              'skipping %s.' % name)
    for file in atom.files:
        destination = file.destination
        extension = os.path.splitext(destination)[1][1:]
        if extension == 'so':
            dest = os.path.join(output, 'arch', metadata['target-arch'],
                                destination)
            make_dir(dest)
            shutil.copyfile(file.source, dest)
        elif extension == 'h' or extension == 'modulemap':
            dest = os.path.join(output, 'pkg', name, destination)
            make_dir(dest)
            shutil.copyfile(file.source, dest)
        else:
            raise Exception('Error: unknow file extension "%s" for %s.' %
                            (extension, name))


def install_cpp_source_atom(atom, metadata, output):
    '''Installs a source atom from the "c-pp" domain.'''
    name = atom.id.name
    for file in atom.files:
        dest = os.path.join(output, 'pkg', name, file.destination)
        make_dir(dest)
        shutil.copyfile(file.source, dest)


def install_exe_atom(atom, metadata, output):
    '''Installs an atom from the "exe" domain.'''
    name = atom.id.name
    if atom.tags['arch'] != 'host':
        print('Only host executables are supported, skipping %s.' % name)
        return
    files = atom.files
    if len(files) != 1:
        raise Exception('Error: executable with multiple files: %s' % name)
    file = files[0]
    destination = os.path.join(output, 'tools', file.destination)
    make_dir(destination)
    shutil.copyfile(file.source, destination)


def main():
    parser = argparse.ArgumentParser(
            description=('Lays out an SDK based on the given manifest'))
    parser.add_argument('--manifest',
                        help='Path to the SDK manifest',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the directory where to install the SDK',
                        required=True)
    args = parser.parse_args()

    # Remove any existing output.
    shutil.rmtree(args.output, True)

    # Read the contents of the manifest.
    with open(args.manifest, 'r') as manifest_file:
        manifest = json.load(manifest_file)
    atoms = [Atom(a) for a in manifest['atoms']]
    metadata = manifest['meta']

    # Verify that the manifest only contains supporterd domains.
    supported_domains = {
        'c-pp',
        'exe',
    }
    unknown_domains = set(filter(lambda d: d not in supported_domains,
                                 [a.id.domain for a in atoms]))
    if unknown_domains:
        print('The following domains are not currently supported: %s' %
              ', '.join(unknown_domains))
        return 1

    # Install the various atoms in the output directory.
    for atom in atoms:
        domain = atom.id.domain
        if domain == 'c-pp':
            install_cpp_atom(atom, metadata, args.output)
        elif domain == 'exe':
            install_exe_atom(atom, metadata, args.output)
        else:
            raise Exception('Unsupported domain: %s' % domain)


if __name__ == '__main__':
    sys.exit(main())
