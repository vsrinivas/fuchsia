#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # bazel
    SCRIPT_DIR)))                # sdk

sys.path += [os.path.join(FUCHSIA_ROOT, "third_party", "mako")]
sys.path += [os.path.join(FUCHSIA_ROOT, "scripts", "sdk")]

from layout_builder import Builder, process_manifest
from mako.template import Template
from os import chmod
import stat


class Library(object):
    '''Represents a C/C++ library.
       Convenience storage object to be consumed by Mako templates.
       '''
    def __init__(self, name):
        self.name = name
        self.srcs = []
        self.hdrs = []
        self.deps = []
        self.includes = []

def remove_dashes(name):
    return name.replace("-", "_")

class CppBuilder(Builder):

    def __init__(self, output, overlay, debug):
        super(CppBuilder, self).__init__(domains=['cpp', 'exe', 'fidl'])
        self.output = output
        self.is_overlay = overlay
        self.is_debug = debug
        if self.is_debug:
            workspace_filename = os.path.join(self.output, 'WORKSPACE')
            self.make_dir(workspace_filename)
            open(workspace_filename,"w+")


    def install_cpp_atom(self, atom):
        '''Installs an atom from the "cpp" domain.'''
        type = atom.tags['type']
        if type == 'compiled_shared':
            self.install_cpp_prebuilt_atom(atom)
        elif type == 'sources':
            self.install_cpp_source_atom(atom)
        elif type == 'sysroot':
            self.install_sysroot_atom(atom)
        else:
            print('Atom type "%s" not handled, skipping %s.' % (type, atom.id))


    def write_build_file(self, library, type):

        if self.is_debug and type != 'sysroot':
            library.deps.append("//pkg/system:system")

        path = os.path.join(self.output, 'pkg', library.name, 'BUILD')
        build_file = open(path,"w+")

        # in debug mode we just make the sysroot another cc_library
        if type == 'sysroot' and not self.is_debug:
            template = Template(filename=os.path.join(SCRIPT_DIR, 'sysroot.mako'))
            build_file.write(template.render(data=library))
        else:
            library.includes.append(".")
            library.includes.append("./include")
            template = Template(filename=os.path.join(SCRIPT_DIR, 'cc_library.mako'))
            build_file.write(template.render(data=library))


    def install_cpp_prebuilt_atom(self, atom, check_arch=True):
        '''Installs a prebuilt atom from the "cpp" domain.'''
        if check_arch and atom.tags['arch'] != 'target':
            print('Only libraries compiled for a target are supported, '
                  'skipping %s.' % atom.id)
            return

        atom_name = remove_dashes(atom.id.name)

        library = Library(atom_name)

        for file in atom.files:
            destination = file.destination
            extension = os.path.splitext(destination)[1][1:]
            if extension == 'so' or extension == 'o':
                dest = os.path.join(self.output, 'pkg', atom_name,'arch',
                                    self.metadata.target_arch, destination)
                if os.path.isfile(dest):
                    raise Exception('File already exists: %s.' % dest)
                self.make_dir(dest)
                shutil.copyfile(file.source, dest)
                if extension == 'so' and destination.startswith("lib"):
                    library.srcs.append(os.path.join('arch',
                                    self.metadata.target_arch, destination))

            elif self.is_overlay:
                # Only binaries get installed in overlay mode.
                continue
            elif (extension == 'h' or extension == 'modulemap' or
                    extension == 'inc' or extension == 'rs'):
                dest = os.path.join(self.output, 'pkg', atom_name,
                                    destination)
                self.make_dir(dest)
                shutil.copyfile(file.source, dest)
                if extension == 'h':
                    library.hdrs.append(destination)
            else:
                raise Exception('Error: unknow file extension "%s" for %s.' %
                                (extension, atom.id))
        for dep_id in atom.deps:
            dep_name = remove_dashes(dep_id.name)
            dep = os.path.join("//pkg/", dep_name)
            library.deps.append(dep)

        self.write_build_file(library, atom.tags['type'])


    def install_cpp_source_atom(self, atom):
        '''Installs a source atom from the "cpp" domain.'''

        atom_name = remove_dashes(atom.id.name)

        library = Library(atom_name)

        if self.is_overlay:
            return
        for file in atom.files:
            dest = os.path.join(self.output, 'pkg', atom_name,
                                file.destination)
            self.make_dir(dest)
            shutil.copyfile(file.source, dest)

            extension = os.path.splitext(file.destination)[1][1:]
            if extension == 'h':
                library.hdrs.append(file.destination)
            elif extension == 'c' or extension == 'cc' or extension == 'cpp':
                library.srcs.append(file.destination)
            else:
                raise Exception('Error: unknow file extension "%s" for %s.' %
                                (extension, atom.id))

        for dep_id in atom.deps:
            dep = os.path.join("//pkg/", dep_id.name)
            library.deps.append(dep)


        self.write_build_file(library, atom.tags['type'])


    def install_sysroot_atom(self, atom):
        self.install_cpp_prebuilt_atom(atom, check_arch=False)


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
        shutil.copyfile(file.source, destination)
        st = os.stat(destination)
        os.chmod(destination, st.st_mode | stat.S_IXUSR | stat.S_IXGRP)

    def install_fidl_atom(self, atom): pass



def main():
    parser = argparse.ArgumentParser(
            description=('Lays out an SDK based on the given manifest'))
    parser.add_argument('--manifest',
                        help='Path to the SDK manifest',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the directory where to install the SDK',
                        required=True)
    parser.add_argument('--debug',
                        help='Set up generated build files for standalone building',
                        action='store_true')
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

    builder = CppBuilder(args.output, args.overlay, args.debug)
    return 0 if process_manifest(args.manifest, builder) else 1


if __name__ == '__main__':
    sys.exit(main())
