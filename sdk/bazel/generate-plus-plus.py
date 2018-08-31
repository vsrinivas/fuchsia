#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import stat
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # sdk
    SCRIPT_DIR)))                # bazel

sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'mako')]
from mako.lookup import TemplateLookup
from mako.template import Template
sys.path += [os.path.join(FUCHSIA_ROOT, 'scripts', 'sdk', 'common')]
from frontend import Frontend

import template_model as model


ARCH_MAP = {
    'arm64': 'aarch64',
    'x64': 'x86_64',
}


def sanitize(name):
    return name.replace('-', '_').replace('.', '_')


class BazelBuilder(Frontend):

    def __init__(self, archive, output):
        super(BazelBuilder, self).__init__(archive, output)
        self.dart_vendor_packages = {}


    def _bail(self):
        '''Temporary method to abort execution when hitting a code path that's
        not yet been converted.
        '''
        raise Exception('Not implemented!')


    def local(self, *args):
        '''Builds a path in the current directory.'''
        return os.path.join(SCRIPT_DIR, *args)


    def write_file(self, path, template_name, data, append=False):
        '''Writes a file based on a Mako template.'''
        lookup = TemplateLookup(directories=[self.local('templates')])
        template = lookup.get_template(template_name + '.mako')
        with open(path, 'a' if append else 'w') as file:
            file.write(template.render(data=data))


    def add_dart_vendor_package(self, name, version):
        '''Adds a reference to a new Dart third-party package.'''
        if name == 'flutter' and version == 'flutter_sdk':
            # The Flutter SDK is set up separately.
            return
        if name in self.dart_vendor_packages:
            existing_version = self.dart_vendor_packages[name]
            if existing_version != version:
                raise Exception('Dart package %s can only have one version; '
                                '%s and %s requested.' % (name, version,
                                                          existing_version))
        else:
            self.dart_vendor_packages[name] = version


    def prepare(self):
        # Copy the static files.
        shutil.copytree(self.local('base'), self.output)


    def finalize(self):
        # Generate the crosstool.bzl based on the available sysroots.
        self.install_crosstool()
        # Write BUILD files for tools directories.
        tools_root = os.path.join(self.output, 'tools')
        for directory, _, _ in os.walk(tools_root, topdown=True):
            self.write_file(os.path.join(directory, 'BUILD'), 'tools', {})
        # Write the rule for setting up Dart packages.
        # TODO(pylaligand): this process currently does not capture dependencies
        # between vendor packages.
        self.write_file(self.dest('build_defs', 'setup_dart.bzl'),
                       'setup_dart_bzl', self.dart_vendor_packages)


    def install_crosstool(self):
        sysroot_dir = self.dest('arch')
        if not os.path.isdir(sysroot_dir):
            return
        crosstool = model.Crosstool()
        for arch in os.listdir(sysroot_dir):
            if arch in ARCH_MAP:
                crosstool.arches.append(model.Arch(arch, ARCH_MAP[arch]))
            else:
                print('Unknown target arch: %s' % arch)
        self.write_file(self.dest('build_defs', 'crosstool.bzl'),
                        'crosstool_bzl', crosstool)
        self.write_file(self.dest('build_defs', 'BUILD.crosstool'),
                        'crosstool', crosstool)
        self.write_file(self.dest('build_defs', 'CROSSTOOL.in'),
                        'crosstool_in', crosstool)
        self.write_file(self.dest('build_defs', 'toolchain', 'BUILD'),
                        'toolchain_build', crosstool)


    def install_dart_library_atom(self, atom):
        package_name = atom['name']
        name = sanitize(package_name)
        library = model.DartLibrary(name, package_name)
        base = self.dest('dart', name)

        for file in atom['sources']:
            relative_path = os.path.relpath(file, atom['root'])
            dest = self.make_dir(os.path.join(base, relative_path))
            shutil.copy2(self.source(file), dest)

        for dep in atom['deps']:
            library.deps.append('//dart/' + sanitize(dep))

        for dep in atom['third_party_deps']:
            name = dep['name']
            library.deps.append('@vendor_%s//:%s' % (name, name))
            self.add_dart_vendor_package(name, dep['version'])

        self.write_file(os.path.join(base, 'BUILD'), 'dart_library', library)


    def install_cpp_prebuilt_atom(self, atom, check_arch=True):
        '''Installs a prebuilt atom from the "cpp" domain.'''
        self._bail()
        if check_arch and atom.tags['arch'] != 'target':
            print('Only libraries compiled for a target are supported, '
                  'skipping %s.' % atom.id)
            return

        name = sanitize(atom.id.name)
        library = model.CppPrebuiltLibrary(name, self.metadata.target_arch)
        base = self.dest('pkg', name)

        for file in atom.files:
            destination = file.destination
            extension = os.path.splitext(destination)[1][1:]
            if extension == 'so' or extension == 'a' or extension == 'o':
                relative_dest = os.path.join('arch', self.metadata.target_arch,
                                             destination)
                dest = os.path.join(base, relative_dest)
                if os.path.isfile(dest):
                    raise Exception('File already exists: %s.' % dest)
                self.make_dir(dest)
                shutil.copy2(file.source, dest)
                if ((extension == 'so' or extension == 'a') and
                        destination.startswith('lib')):
                    if library.prebuilt:
                        raise Exception('Multiple prebuilts for %s.' % dest)
                    library.prebuilt = relative_dest
                    library.is_static = extension == 'a'
                if file.is_packaged:
                    package_path = 'lib/%s' % os.path.basename(relative_dest)
                    library.packaged_files[package_path] = relative_dest
            elif self.is_overlay:
                # Only binaries get installed in overlay mode.
                continue
            elif (extension == 'h' or extension == 'modulemap' or
                    extension == 'inc' or extension == 'rs'):
                dest = self.make_dir(os.path.join(base, destination))
                shutil.copy2(file.source, dest)
                if extension == 'h':
                    library.hdrs.append(destination)
            else:
                dest = self.make_dir(os.path.join(base, destination))
                shutil.copy2(file.source, dest)

        for dep_id in atom.deps:
            library.deps.append('//pkg/' + sanitize(dep_id.name))
        library.includes.append('include')

        # Only write the prebuilt library BUILD top when not in overlay mode.
        if not self.is_overlay:
            self.write_file(os.path.join(base, 'BUILD'),
                            'cc_prebuilt_library_top', library)

        # Write the arch-specific target.
        self.write_file(os.path.join(base, 'BUILD'),
                        'cc_prebuilt_library_srcs', library, append=True)


    def install_cpp_source_atom(self, atom):
        '''Installs a source atom from the "cpp" domain.'''
        self._bail()

        name = sanitize(atom.id.name)
        library = model.CppSourceLibrary(name)
        base = self.dest('pkg', name)

        for file in atom.files:
            dest = self.make_dir(os.path.join(base, file.destination))
            shutil.copy2(file.source, dest)
            extension = os.path.splitext(file.destination)[1][1:]
            if extension == 'h':
                library.hdrs.append(file.destination)
            elif extension == 'c' or extension == 'cc' or extension == 'cpp':
                library.srcs.append(file.destination)
            elif extension == 'json':
                continue
            else:
                raise Exception('Error: unknow file extension "%s" for %s.' %
                                (extension, atom.id))

        for dep_id in atom.deps:
            library.deps.append('//pkg/' + sanitize(dep_id.name))

        library.includes.append('include')

        self.write_file(os.path.join(base, 'BUILD'), 'cc_library', library)


    def install_cpp_sysroot_atom(self, atom):
        '''Installs a sysroot atom from the "cpp" domain.'''
        self._bail()

        data = model.Sysroot(self.metadata.target_arch)

        base = self.dest('arch', self.metadata.target_arch, 'sysroot')
        for file in atom.files:
            dest = self.make_dir(os.path.join(base, file.destination))
            shutil.copy2(file.source, dest)
            if file.is_packaged:
                package_path = 'lib/%s' % os.path.basename(file.destination)
                data.packaged_files[package_path] = file.destination
        self.write_file(os.path.join(base, 'BUILD'), 'sysroot_arch', data)

        if not self.is_overlay:
            self.write_file(self.dest('pkg', 'sysroot', 'BUILD'),
                            'sysroot_pkg_top', data)
        self.write_file(self.dest('pkg', 'sysroot', 'BUILD'),
                        'sysroot_pkg_dist', data, append=True)


    def install_exe_atom(self, atom):
        '''Installs an atom from the "exe" domain.'''
        self._bail()
        if atom.tags['arch'] != 'host':
            print('Only host executables are supported, skipping %s.' % atom.id)
            return
        if self.is_overlay:
            return
        for file in atom.files:
            extension = os.path.splitext(file.destination)[1][1:]
            if extension == 'json':
                continue
            destination = self.make_dir(self.dest('tools', file.destination))
            shutil.copy2(file.source, destination)


    def install_fidl_atom(self, atom):
        '''Installs an atom from the "fidl" domain.'''
        self._bail()
        name = sanitize(atom.id.name)
        data = model.FidlLibrary(name, atom.tags['name'])
        base = self.dest('fidl', name)
        for file in atom.files:
            extension = os.path.splitext(file.destination)[1][1:]
            if extension == 'json':
                continue
            dest = self.make_dir(os.path.join(base, file.destination))
            shutil.copy2(file.source, dest)
            data.srcs.append(file.destination)
        for dep_id in atom.deps:
            data.deps.append('//fidl/' + sanitize(dep_id.name))
        self.write_file(os.path.join(base, 'BUILD'), 'fidl', data)


def main():
    parser = argparse.ArgumentParser(
            description=('Lays out a Bazel workspace for a given SDK tarball.'))
    parser.add_argument('--archive',
                        help='Path to the SDK archive to ingest',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the directory where to install the SDK',
                        required=True)
    args = parser.parse_args()

    # Remove any existing output.
    shutil.rmtree(args.output, True)

    builder = BazelBuilder(args.archive, args.output)
    return 0 if builder.run() else 1


if __name__ == '__main__':
    sys.exit(main())
