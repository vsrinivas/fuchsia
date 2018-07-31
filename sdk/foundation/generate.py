#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import sys

FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # sdk
    os.path.dirname(             # foundation
    os.path.abspath(__file__)))))

sys.path += [os.path.join(FUCHSIA_ROOT, 'scripts', 'sdk', 'common')]
from layout_builder import Builder, process_manifest


class CppBuilder(Builder):

    def __init__(self, output, overlay):
        super(CppBuilder, self).__init__(
            domains=['cpp', 'exe', 'fidl', 'image'],
            ignored_domains=['dart'])
        self.output = output
        self.is_overlay = overlay


    def install_cpp_atom(self, atom):
        '''Installs an atom from the "cpp" domain.'''
        type = atom.tags['type']
        if type == 'compiled_shared' or type == 'compiled_static':
            self.install_cpp_prebuilt_atom(atom)
        elif type == 'sources':
            self.install_cpp_source_atom(atom)
        elif type == 'sysroot':
            self.install_cpp_sysroot_atom(atom)
        else:
            print('Atom type "%s" not handled, skipping %s.' % (type, atom.id))


    def install_cpp_prebuilt_atom(self, atom):
        '''Installs a prebuilt atom from the "cpp" domain.'''
        if atom.tags['arch'] != 'target':
            print('Only libraries compiled for a target are supported, '
                  'skipping %s.' % atom.id)
            return
        for file in atom.files:
            destination = file.destination
            extension = os.path.splitext(destination)[1][1:]
            if extension == 'so' or extension == "a" or extension == "o":
                dest = os.path.join(self.output, 'arch',
                                    self.metadata.target_arch, destination)
                if os.path.isfile(dest):
                    raise Exception('File already exists: %s.' % dest)
                self.make_dir(dest)
                shutil.copy2(file.source, dest)
            elif self.is_overlay:
                # Only binaries get installed in overlay mode.
                continue
            elif (extension == 'h' or extension == 'modulemap' or
                    extension == 'inc' or extension == 'rs'):
                dest = os.path.join(self.output, 'pkg', atom.id.name,
                                    destination)
                self.make_dir(dest)
                shutil.copy2(file.source, dest)
            else:
                dest = os.path.join(self.output, 'pkg', atom.id.name,
                        destination)
                self.make_dir(dest)
                shutil.copy2(file.source, dest)


    def install_cpp_source_atom(self, atom):
        '''Installs a source atom from the "cpp" domain.'''
        if self.is_overlay:
            return
        for file in atom.files:
            dest = os.path.join(self.output, 'pkg', atom.id.name,
                                file.destination)
            self.make_dir(dest)
            shutil.copy2(file.source, dest)


    def install_cpp_sysroot_atom(self, atom):
        '''Installs a sysroot atom from the "cpp" domain.'''
        base = os.path.join(self.output, 'arch', self.metadata.target_arch,
                            'sysroot')
        for file in atom.files:
            dest = os.path.join(base, file.destination)
            self.make_dir(dest)
            shutil.copy2(file.source, dest)


    def install_exe_atom(self, atom):
        '''Installs an atom from the "exe" domain.'''
        if atom.tags['arch'] != 'host':
            print('Only host executables are supported, skipping %s.' %
                  atom.id)
            return
        if self.is_overlay:
            return
        files = atom.files
        if len(files) != 1:
            raise Exception('Error: executable with multiple files: %s.'
                            % atom.id)
        file = files[0]
        destination = os.path.join(self.output, 'tools', file.destination)
        self.make_dir(destination)
        shutil.copy2(file.source, destination)


    def install_fidl_atom(self, atom):
        '''Installs an atom from the "fidl" domain.'''
        if self.is_overlay:
            return
        for file in atom.files:
            dest = os.path.join(self.output, 'fidl', atom.id.name,
                                file.destination)
            self.make_dir(dest)
            shutil.copy2(file.source, dest)


    def install_image_atom(self, atom):
        '''Installs an atom from the "image" domain.'''
        for file in atom.files:
            dest = os.path.join(self.output, 'target',
                                self.metadata.target_arch,
                                file.destination)
            self.make_dir(dest)
            shutil.copy2(file.source, dest)


def main():
    parser = argparse.ArgumentParser(
            description=('Lays out an SDK based on the given manifest'))
    parser.add_argument('--manifest',
                        help='Path to the SDK manifest',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the directory where to install the SDK',
                        required=True)
    parser.add_argument('--overlay',
                        help='Whether to overlay target binaries on top of an '
                             'existing layout',
                        action='store_true')
    args = parser.parse_args()

    # Remove any existing output.
    if args.overlay:
        if not os.path.isdir(args.output) :
            print('Cannot overlay on top of missing output directory: %s.' %
                  args.output)
            return 1
    else:
        shutil.rmtree(args.output, True)

    builder = CppBuilder(args.output, args.overlay)
    return 0 if process_manifest(args.manifest, builder) else 1


if __name__ == '__main__':
    sys.exit(main())
