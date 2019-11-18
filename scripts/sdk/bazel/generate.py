#!/usr/bin/env python2.7
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
from files import copy_tree

from create_test_workspace import create_test_workspace, SdkWorkspaceInfo
import template_model as model


def sanitize(name):
    return name.replace('-', '_').replace('.', '_')


class BazelBuilder(Frontend):

    def __init__(self, **kwargs):
        super(BazelBuilder, self).__init__(**kwargs)
        self.has_dart = False
        self.has_cc = False
        self.dart_vendor_packages = {}
        self.target_arches = []
        self.workspace_info = SdkWorkspaceInfo()


    def _copy_file(self, file, root='', destination='', result=[]):
        '''Copies the file from a given root directory and writes the
        resulting relative paths to a list.
        '''
        if os.path.commonprefix([root, file]) != root:
            raise Exception('%s is not within %s' % (file, root))
        relative_path = os.path.relpath(file, root)
        dest = self.dest(destination, relative_path)
        shutil.copy2(self.source(file), dest)
        result.append(relative_path)


    def _copy_files(self, files, root='', destination='', result=[]):
        '''Copies some files from a given root directory and writes the
        resulting relative paths to a list.
        '''
        for file in files:
            self._copy_file(file, root, destination, result)


    def local(self, *args):
        '''Builds a path in the current directory.'''
        return os.path.join(SCRIPT_DIR, *args)


    def write_file(self, path, template_name, data):
        '''Writes a file based on a Mako template.'''
        lookup = TemplateLookup(directories=[self.local('templates')])
        template = lookup.get_template(template_name + '.mako')
        with open(path, 'w') as file:
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


    def prepare(self, arch, types):
        self.target_arches = arch['target']

        # Copy the common files.
        shutil.copytree(self.local('base', 'common'), self.output)
        # Copy C/C++ files if needed.
        if ('sysroot' in types or 'cc_source_library' in types or
            'cc_prebuilt_library' in types):
            self.has_cc = True
            copy_tree(self.local('base', 'cc'), self.output)
        # Copy Dart files if needed.
        if 'dart_library' in types:
            # fxb/41514 : disable dart/flutter bits
            # self.has_dart = True
            # copy_tree(self.local('base', 'dart'), self.output)
            pass

        self.install_crosstool(arch)

        self.workspace_info.with_cc = self.has_cc
        self.workspace_info.with_dart = self.has_dart
        self.workspace_info.target_arches = self.target_arches


    def finalize(self, arch, types):
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
        if not self.has_cc:
            return
        crosstool = model.Crosstool(arches['target'])
        crosstool_base = self.dest('build_defs', 'internal', 'crosstool')
        self.write_file(self.dest(crosstool_base, 'crosstool.bzl'),
                        'crosstool_bzl', crosstool)
        self.write_file(self.dest(crosstool_base, 'BUILD.crosstool'),
                        'crosstool', crosstool)
        self.write_file(self.dest(crosstool_base, 'CROSSTOOL.in'),
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
        package_name = atom['name']
        name = sanitize(package_name)
        library = model.DartLibrary(name, package_name)
        base = self.dest('dart', name)

        self._copy_files(atom['sources'], atom['root'], base)

        for dep in atom['deps']:
            library.deps.append('//dart/' + sanitize(dep))

        for dep in atom['fidl_deps']:
            san_dep = sanitize(dep)
            library.deps.append('//fidl/%s:%s_dart' % (san_dep, san_dep))

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

            if 'debug' in binaries:
                self._copy_file(binaries['debug'])

            library.prebuilts[arch] = prebuilt_set

        for dep in atom['deps']:
            library.deps.append('//pkg/' + sanitize(dep))

        library.includes = os.path.relpath(atom['include_dir'], atom['root'])

        include_paths = map(lambda h: os.path.relpath(h, atom['include_dir']),
                            atom['headers'])
        self.workspace_info.headers['//pkg/' + name] = include_paths

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
            dep_name = sanitize(dep)
            library.deps.append('//fidl/%s:%s_cc' % (dep_name, dep_name))

        library.includes = os.path.relpath(atom['include_dir'], atom['root'])

        include_paths = map(lambda h: os.path.relpath(h, atom['include_dir']),
                            atom['headers'])
        self.workspace_info.headers['//pkg/' + name] = include_paths

        self.write_file(os.path.join(base, 'BUILD'), 'cc_library', library)


    def install_sysroot_atom(self, atom):
        for arch in self.target_arches:
            base = self.dest('arch', arch, 'sysroot')
            arch_data = atom['versions'][arch]
            self._copy_files(arch_data['headers'], arch_data['root'], base)
            self._copy_files(arch_data['link_libs'], arch_data['root'], base)
            # We maintain debug files in their original location.
            self._copy_files(arch_data['debug_libs'])
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
        if 'files' in atom:
            self._copy_files(atom['files'], atom['root'], 'tools')
        if 'target_files' in atom:
            for files in atom['target_files'].itervalues():
                self._copy_files(files, atom['root'], 'tools')


    def install_fidl_library_atom(self, atom):
        name = sanitize(atom['name'])
        data = model.FidlLibrary(name, atom['name'])
        data.with_cc = self.has_cc
        data.with_dart = self.has_dart
        base = self.dest('fidl', name)
        self._copy_files(atom['sources'], atom['root'], base, data.srcs)
        for dep in atom['deps']:
            data.deps.append(sanitize(dep))
        self.write_file(os.path.join(base, 'BUILD'), 'fidl', data)
        self.workspace_info.fidl_libraries.append(name);


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
    parser.add_argument('--tests',
                        help='Path to the directory where to generate tests')
    args = parser.parse_args()

    # Remove any existing output.
    shutil.rmtree(args.output, ignore_errors=True)

    builder = BazelBuilder(archive=args.archive,
                           directory=args.directory,
                           output=args.output)
    if not builder.run():
        return 1

    if args.tests and not create_test_workspace(args.output, args.tests,
                                                builder.workspace_info):
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
