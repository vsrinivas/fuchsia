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

    def __init__(self, **kwargs):
        super(BazelBuilder, self).__init__(**kwargs)
        self.has_dart = False
        self.has_sysroot = False
        self.dart_vendor_packages = {}
        self.target_arches = []


    def _copy_file(self, file, root, destination, result=[]):
        '''Copies the file from a given root directory and writes the
        resulting relative paths to a list.
        '''
        if os.path.commonprefix([root, file]) != root:
            raise Exception('%s is not within %s' % (file, root))
        relative_path = os.path.relpath(file, root)
        dest = self.dest(destination, relative_path)
        shutil.copy2(self.source(file), dest)
        result.append(relative_path)


    def _copy_files(self, files, root, destination, result=[]):
        '''Copies some files from a given root directory and writes the
        resulting relative paths to a list.
        '''
        for file in files:
            self._copy_file(file, root, destination, result)


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


    def prepare(self, arch):
        self.target_arches = arch['target']
        # Copy the static files.
        shutil.copytree(self.local('base'), self.output)


    def finalize(self, arch):
        self.install_crosstool(arch)
        self.install_tools()
        self.install_dart()


    def install_tools(self):
        '''Write BUILD files for tools directories.'''
        tools_root = os.path.join(self.output, 'tools')
        for directory, _, _ in os.walk(tools_root, topdown=True):
            self.write_file(os.path.join(directory, 'BUILD'), 'tools', {})


    def install_crosstool(self, arches):
        '''Generates crosstool.bzl based on the availability of sysroot
        versions.
        '''
        if not self.has_sysroot:
            return
        crosstool = model.Crosstool()
        for arch in arches['target']:
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


    def install_dart(self):
        if not self.has_dart:
            return
        # Write the rule for setting up Dart packages.
        # TODO(pylaligand): this process currently does not capture dependencies
        # between vendor packages.
        self.write_file(self.dest('build_defs', 'setup_dart.bzl'),
                       'setup_dart_bzl', self.dart_vendor_packages)


    def install_dart_library_atom(self, atom):
        self.has_dart = True

        package_name = atom['name']
        name = sanitize(package_name)
        library = model.DartLibrary(name, package_name)
        base = self.dest('dart', name)

        self._copy_files(atom['sources'], atom['root'], base)

        for dep in atom['deps']:
            library.deps.append('//dart/' + sanitize(dep))

        for dep in atom['third_party_deps']:
            name = dep['name']
            library.deps.append('@vendor_%s//:%s' % (name, name))
            self.add_dart_vendor_package(name, dep['version'])

        self.write_file(os.path.join(base, 'BUILD'), 'dart_library', library)


    def install_cc_prebuilt_library_atom(self, atom):
        name = sanitize(atom['name'])
        library = model.CppPrebuiltLibrary(name)
        library.is_static = atom['format'] == 'static'
        base = self.dest('pkg', name)

        self._copy_files(atom['headers'], atom['root'], base, library.hdrs)

        for arch in self.target_arches:
            def _copy_prebuilt(path, category):
                relative_dest = os.path.join('arch', arch, category,
                                             os.path.basename(path))
                dest = self.dest(base, relative_dest)
                shutil.copy2(self.source(path), dest)
                return relative_dest

            binaries = atom['binaries'][arch]
            prebuilt_set = model.CppPrebuiltSet(_copy_prebuilt(binaries['link'],
                                                              'lib'))
            if 'dist' in binaries:
                dist = binaries['dist']
                prebuilt_set.dist_lib = _copy_prebuilt(dist, 'dist')
                prebuilt_set.dist_path = 'lib/' + os.path.basename(dist)

            # TODO(DX-340): this is only to reach parity with the old version of
            # the frontend, should be removed.
            if 'debug' in binaries:
                _copy_prebuilt(binaries['debug'], 'debug')

            library.prebuilts[arch] = prebuilt_set

        for dep in atom['deps']:
            library.deps.append('//pkg/' + sanitize(dep))

        library.includes.append(os.path.relpath(atom['include_dir'],
                                                atom['root']))

        # TODO(DX-340): remove the _top and _srcs templates.
        self.write_file(os.path.join(base, 'BUILD'), 'cc_prebuilt_library',
                        library)


    def install_cc_source_library_atom(self, atom):
        name = sanitize(atom['name'])
        library = model.CppSourceLibrary(name)
        base = self.dest('pkg', name)

        self._copy_files(atom['headers'], atom['root'], base, library.hdrs)
        self._copy_files(atom['sources'], atom['root'], base, library.srcs)

        for dep in atom['deps']:
            library.deps.append('//pkg/' + sanitize(dep))

        for dep in atom['fidl_deps']:
            name = sanitize(dep)
            library.deps.append('//fidl/%s:%s_cc' % (name, name))

        library.includes.append(os.path.relpath(atom['include_dir'],
                                                atom['root']))

        self.write_file(os.path.join(base, 'BUILD'), 'cc_library', library)


    def install_sysroot_atom(self, atom):
        self.has_sysroot = True
        for arch in self.target_arches:
            base = self.dest('arch', arch, 'sysroot')
            arch_data = atom['versions'][arch]
            self._copy_files(arch_data['headers'], arch_data['root'], base)
            self._copy_files(arch_data['link_libs'], arch_data['root'], base)
            self._copy_files(arch_data['debug_libs'], arch_data['root'], base)
            dist_libs = []
            self._copy_files(arch_data['dist_libs'], arch_data['root'], base,
                             dist_libs)
            version = {}
            for lib in dist_libs:
                version['lib/' + os.path.basename(lib)] = lib
            self.write_file(os.path.join(base, 'BUILD'), 'sysroot_version',
                            version)

        self.write_file(self.dest('pkg', 'sysroot', 'BUILD'),
                        'sysroot_pkg', self.target_arches)


    def install_host_tool_atom(self, atom):
        self._copy_files(atom['files'], atom['root'], 'tools')


    def install_fidl_library_atom(self, atom):
        name = sanitize(atom['name'])
        data = model.FidlLibrary(name, atom['name'])
        base = self.dest('fidl', name)
        self._copy_files(atom['sources'], atom['root'], base, data.srcs)
        for dep in atom['deps']:
            data.deps.append(sanitize(dep))
        self.write_file(os.path.join(base, 'BUILD'), 'fidl', data)


    def install_image_atom(self, atom):
        # 'image_file' contains a relative path that is good enough to be stored
        # under the top-level SDK directory. No 'root' or 'destination' is
        # needed.
        root = ''
        dest = ''
        target_architectures = {}
        for image_file in atom['file'].itervalues():
            self._copy_file(image_file, root, dest)
            # The image file looks like this: target/x64/fuchsia.zbi
            # where x64 is the architecture
            target, arch = os.path.split(os.path.dirname(image_file))
            if target not in target_architectures:
                target_architectures[target] = set()
            target_architectures[target].add(arch)

        for target in target_architectures:
            data = model.Images(list(target_architectures[target]))
            self.write_file(self.dest(os.path.join(target, 'BUILD')), 'images',
                            data)


def main():
    parser = argparse.ArgumentParser(
            description='Lays out a Bazel workspace for a given SDK tarball.')
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument('--archive',
                              help='Path to the SDK archive to ingest',
                              default='')
    source_group.add_argument('--directory',
                              help='Path to the SDK directory to ingest',
                              default='')
    parser.add_argument('--output',
                        help='Path to the directory where to install the SDK',
                        required=True)
    args = parser.parse_args()

    # Remove any existing output.
    shutil.rmtree(args.output, ignore_errors=True)

    builder = BazelBuilder(archive=args.archive,
                           directory=args.directory,
                           output=args.output)
    return 0 if builder.run() else 1


if __name__ == '__main__':
    sys.exit(main())
