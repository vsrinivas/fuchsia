#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import errno
import os
import re
import shutil
import subprocess
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # build
        SCRIPT_DIR))                 # zircon
ZIRCON_ROOT = os.path.join(FUCHSIA_ROOT, 'zircon')

sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'mako')]
from mako.lookup import TemplateLookup
from mako.template import Template


# Packages included in the sysroot.
SYSROOT_PACKAGES = ['c', 'zircon']

# Prefixes of Zircon headers that should not appear in SDKs.
NON_SDK_SYSROOT_HEADER_PREFIXES = [
    'zircon/device',
    'zircon/syscalls/definitions.rs',
]
# TODO(FIDL-273): remove this allowlist.
MANDATORY_SDK_HEADERS = [
    'zircon/device/ioctl.h',         # Needed by zircon/device/ramdisk.h
    'zircon/device/ioctl-wrapper.h', # Needed by zircon/device/ramdisk.h
    # TODO(ZX-2503): remove this entry.
    'zircon/device/ramdisk.h',       # Needed by fs-management/ramdisk.h
    'zircon/device/sysinfo.h',       # Needed by some external clients
]

# List of libraries with header files being transitioned from
# 'include/foo/foo.h' to 'include/lib/foo/foo.h'. During the transition, both
# the library's 'include/' and 'include/lib' directories are added to the
# include path so both old and new style include work.
# TODO(ZX-1871): Once everything in Zircon is migrated, remove this mechanism.
LIBRARIES_BEING_MOVED = []

# Prebuilt libraries for which headers shouldn't be included in an SDK.
# While this kind of mechanism exists in the GN build, there's no equivalent in
# the make build and we have to manually curate these libraries.
LIBRARIES_WITHOUT_SDK_HEADERS = ['trace-engine']

# Packages which should never appear in SDKs.
# Dependencies on these packages will not be reflected in generated rules. The
# lack of distinction between public and private deps in the make build system
# is the reason why this list is needed.
NON_SDK_DEPS = ['zircon-internal']

# FIDL libraries that may be included in SDKs.
# Note that this is a temporary measure until the Zircon GN build allows us to
# express the same concept of SDK publication.
PUBLISHED_FIDL_LIBRARIES = [
    'fuchsia-cobalt',
    'fuchsia-hardware-ethernet',
    'fuchsia-io',
    'fuchsia-ldsvc',
    'fuchsia-logger',
    'fuchsia-mem',
    'fuchsia-net',
    'fuchsia-process',
    'fuchsia-sysinfo',
    'fuchsia-sysmem',
]

def make_dir(path, is_dir=False):
    '''Creates the directory at `path`.'''
    target = path if is_dir else os.path.dirname(path)
    try:
        os.makedirs(target)
    except OSError as exception:
        if exception.errno == errno.EEXIST and os.path.isdir(target):
            pass
        else:
            raise


def try_remove(list, element):
    '''Attempts to remove an element from a list, returning `true` if
       successful.'''
    try:
        list.remove(element)
        return True
    except ValueError:
        return False


def parse_package(lines):
    '''Parses the content of a package file.'''
    result = {}
    section_exp = re.compile('^\[([^\]]+)\]$')
    attr_exp = re.compile('^([^=]+)=(.*)$')
    current_section = None

    def finalize_section():
        if not current_section:
            return
        if current_list and current_map:
            raise Exception('Found both map-style and list-style section')
        if current_map:
            result[current_section] = current_map
        elif current_list:
            result[current_section] = current_list
    for line in lines:
        section_match = section_exp.match(line)
        if section_match:
            finalize_section()
            current_section = section_match.group(1)
            current_list = []
            current_map = {}
            continue
        attr_match = attr_exp.match(line)
        if attr_match:
            name = attr_match.group(1)
            value = attr_match.group(2)
            current_map[name] = value
        else:
            current_list.append(line.strip())
    finalize_section()
    return result


def extract_file(name, path, context, is_tool=False):
    '''Extracts file path and base folder path from a map entry.'''
    # name: foo/bar.h
    # path: <SOURCE|BUILD>/somewhere/under/zircon/foo/bar.h
    (full_path, changes) = re.subn('^SOURCE', context.source_base, path)
    if not changes:
        (full_path, changes) = re.subn('^BUILD', context.build_base, path)
    if not changes:
        raise Exception('Unknown pattern type: %s' % path)
    folder = None
    if full_path.endswith(name):
        folder = os.path.relpath(full_path[:-len(name)], FUCHSIA_ROOT)
    file = os.path.relpath(full_path, FUCHSIA_ROOT)
    return (file, folder)


def filter_deps(deps):
    '''Sanitizes a given dependency list.'''
    return filter(lambda x: x not in SYSROOT_PACKAGES, deps)


def filter_sdk_deps(deps):
    '''Sanitizes a given SDK dependency list.'''
    return filter(lambda x: x not in NON_SDK_DEPS, deps)


def generate_build_file(path, template_name, data, context):
    '''Creates a build file based on a template.'''
    make_dir(path)
    template = context.templates.get_template(template_name)
    contents = template.render(data=data)
    with open(path, 'w') as build_file:
        build_file.write(contents)


class SourceLibrary(object):
    '''Represents a library built from sources.

       Convenience storage object to be consumed by Mako templates.'''

    def __init__(self, name):
        self.name = name
        self.includes = {}
        self.include_dirs = set()
        self.sources = {}
        self.deps = []
        self.sdk_deps = []
        self.fidl_deps = []
        self.banjo_deps = []
        self.libs = set()
        self.depends_on_zircon = False


def generate_source_library(package, context):
    '''Generates the build glue for a library whose sources are provided.'''
    lib_name = package['package']['name']
    data = SourceLibrary(lib_name)

    # Includes.
    for name, path in package.get('includes', {}).iteritems():
        (file, folder) = extract_file(name, path, context)
        data.includes[name] = '//%s' % file
        data.include_dirs.add('//%s' % folder)
        if lib_name in LIBRARIES_BEING_MOVED:
            data.include_dirs.add('//%s/lib' % folder)

    # Source files.
    for name, path in package.get('src', {}).iteritems():
        (file, _) = extract_file(name, path, context)
        data.sources[name] = '//%s' % file

    # Dependencies.
    data.deps += filter_deps(package.get('deps', []))
    data.deps += filter_deps(package.get('static-deps', []))
    data.fidl_deps = filter_deps(package.get('fidl-deps', []))
    data.banjo_deps = filter_deps(package.get('banjo-deps', []))
    data.sdk_deps = filter_sdk_deps(data.deps)

    # Special hack for zircon library dependency: enables special codegen
    # in template depending on whether we're building on Fuchsia or not.
    data.depends_on_zircon = 'zircon' in package.get('deps', [])

    # Generate the build file.
    build_path = os.path.join(context.out_dir, 'lib', lib_name, 'BUILD.gn')
    generate_build_file(build_path, 'source_library.mako', data, context)


class CompiledLibrary(object):
    '''Represents a library already compiled by the Zircon build.

       Convenience storage object to be consumed by Mako templates.'''

    def __init__(self, name, with_sdk_headers):
        self.name = name
        self.includes = {}
        self.include_dirs = set()
        self.deps = []
        self.sdk_deps = []
        self.fidl_deps = []
        self.banjo_deps = []
        self.lib_name = ''
        self.has_impl_prebuilt = False
        self.impl_prebuilt = ''
        self.prebuilt = ''
        self.debug_prebuilt = ''
        self.with_sdk_headers = with_sdk_headers


def generate_compiled_library(package, context):
    '''Generates the build glue for a prebuilt library.'''
    lib_name = package['package']['name']
    data = CompiledLibrary(lib_name,
                           lib_name not in LIBRARIES_WITHOUT_SDK_HEADERS)

    # Includes.
    for name, path in package.get('includes', {}).iteritems():
        (file, folder) = extract_file(name, path, context)
        data.include_dirs.add('//%s' % folder)
        data.includes[name] = '//%s' % file

    # Lib.
    libs = package.get('lib', {})
    if len(libs) == 1:
        # Static library.
        is_shared = False
        (name, path) = libs.items()[0]
        (file, _) = extract_file(name, path, context)
        data.prebuilt = '//%s' % file
        data.lib_name = os.path.basename(name)
    # TODO(jamesr): Delete the == 2 path once Zircon rolls up through all layers
    elif len(libs) == 2 or len(libs) == 3:
        # Shared library.
        is_shared = True
        for name, path in libs.iteritems():
            (file, _) = extract_file(name, path, context)
            if 'debug/' in name:
                data.debug_prebuilt = '//%s' % file
                data.lib_name = os.path.basename(name)
            elif 'dist/lib' in name:
                data.has_impl_prebuilt = True
                data.impl_prebuilt = '//%s' % file
            else:
                data.prebuilt = '//%s' % file
    else:
        raise Exception('Too many files for %s: %s' % (lib_name,
                                                       ', '.join(libs.keys())))

    # Dependencies.
    data.deps += filter_deps(package.get('deps', []))
    data.deps += filter_deps(package.get('static-deps', []))
    data.fidl_deps = filter_deps(package.get('fidl-deps', []))
    data.banjo_deps = filter_deps(package.get('banjo-deps', []))
    data.sdk_deps = filter_sdk_deps(data.deps)

    # Generate the build file.
    template = 'shared_library.mako' if is_shared else 'static_library.mako'
    build_path = os.path.join(context.out_dir, 'lib', lib_name, 'BUILD.gn')
    generate_build_file(build_path, template, data, context)


class Sysroot(object):
    '''Represents the sysroot created by Zircon.

       Convenience storage object to be consumed by Mako templates.'''

    def __init__(self):
        self.files = {}
        self.sdk_files = {}
        self.headers = []
        self.link_libs = []
        self.dist_libs = []
        self.debug_source_libs = []


def generate_sysroot(package, context):
    '''Generates the build glue for the sysroot.'''
    data = Sysroot()

    # Includes.
    for name, path in package.get('includes', {}).iteritems():
        (file, _) = extract_file(name, path, context)
        data.files['include/%s' % name] = '//%s' % file
        in_sdk = True
        for prefix in NON_SDK_SYSROOT_HEADER_PREFIXES:
            if name.startswith(prefix) and name not in MANDATORY_SDK_HEADERS:
                in_sdk = False
                break
        if in_sdk:
            dest = 'include/%s' % name
            data.sdk_files[dest] = '//%s' % file
            data.headers.append(dest)

    # Lib.
    for name, path in package.get('lib', {}).iteritems():
        (file, _) = extract_file(name, path, context)
        label = '//%s' % file
        data.files[name] = label
        type_dir = os.path.dirname(name)
        if type_dir == 'debug':
            data.debug_source_libs.append(label)
        else:
            data.sdk_files[name] = label
            if type_dir == 'lib':
                data.link_libs.append(name)
            elif type_dir == 'dist/lib':
                data.dist_libs.append(name)
            else:
                raise Exception('Unknown library type: ' + type_dir)

    # Generate the build file.
    build_path = os.path.join(context.out_dir, 'sysroot', 'BUILD.gn')
    generate_build_file(build_path, 'sysroot.mako', data, context)


class HostTool(object):
    '''Represents a host tool.

       Convenience storage object to be consumed by Mako templates.'''

    def __init__(self, name):
        self.name = name
        self.executable = ''


def generate_host_tool(package, context):
    '''Generates the build glue for a host tool.'''
    name = package['package']['name']
    data = HostTool(name)

    bins = package.get('bin', {})
    if len(bins) != 1 or name not in bins:
        raise Exception('Host tool %s has unexpected binaries %s.'
                        % (name, bins))
    (file, _) = extract_file(name, bins[name], context, is_tool=True)
    data.executable = '//%s' % file

    # Generate the build file.
    build_path = os.path.join(context.out_dir, 'tool', name, 'BUILD.gn')
    generate_build_file(build_path, 'host_tool.mako', data, context)


class FidlLibrary(object):
    '''Represents a FIDL library.

       Convenience storage object to be consumed by Mako templates.'''

    def __init__(self, name, library):
        self.name = name
        self.library = library
        self.sources = []
        self.fidl_deps = []
        self.is_published = False
        self.api = ''


def generate_fidl_library(package, context):
    '''Generates the build glue for a FIDL library.'''
    pkg_name = package['package']['name']
    # TODO(pylaligand): remove fallback.
    data = FidlLibrary(pkg_name, package['package'].get('library', pkg_name))

    if pkg_name in PUBLISHED_FIDL_LIBRARIES:
        data.is_published = True
        library_name = pkg_name.replace('-', '.')
        data.api = '//sdk/fidl/%s/%s.api' % (library_name, library_name)

    for name, path in package.get('fidl', {}).iteritems():
        (file, _) = extract_file(name, path, context)
        data.sources.append('//%s' % file)
    data.fidl_deps = filter_deps(package.get('fidl-deps', []))

    # Generate the build file.
    build_path = os.path.join(context.out_dir, 'fidl', pkg_name, 'BUILD.gn')
    generate_build_file(build_path, 'fidl.mako', data, context)


class GenerationContext(object):
    '''Describes the context in which GN rules should be generated.'''

    def __init__(self, out_dir, source_base, build_base, templates):
        self.out_dir = out_dir
        self.source_base = source_base
        self.build_base = build_base
        self.templates = templates


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out',
                        help='Path to the output directory',
                        required=True)
    parser.add_argument('--zircon-build',
                        help='Path to the Zircon build directory',
                        required=True)
    parser.add_argument('--zircon-manifest',
                        help='Path to the Zircon export/manifest file',
                        required=True)
    parser.add_argument('--debug',
                        help='Whether to print out debug information',
                        action='store_true')
    args = parser.parse_args()

    out_dir = os.path.abspath(args.out)
    shutil.rmtree(os.path.join(out_dir, 'fidl'), True)
    shutil.rmtree(os.path.join(out_dir, 'lib'), True)
    shutil.rmtree(os.path.join(out_dir, 'sysroot'), True)
    shutil.rmtree(os.path.join(out_dir, 'tool'), True)
    debug = args.debug

    # Parse package definitions from Zircon's build.
    packages = []
    with open(args.zircon_manifest, 'r') as manifest:
        for file in manifest:
            file = file.strip()
            with open(os.path.join(args.zircon_build, 'export', file), 'r') as pkg_file:
                packages.append(parse_package(pkg_file.readlines()))
    if debug:
        print('Found %s packages:' % len(packages))
        names = sorted(map(lambda p: p['package']['name'], packages))
        for name in names:
            print(' - %s' % name)

    # Generate some GN glue for each package.
    context = GenerationContext(
        out_dir,
        ZIRCON_ROOT,
        os.path.abspath(args.zircon_build),
        TemplateLookup(directories=[SCRIPT_DIR]),
    )
    for package in packages:
        name = package['package']['name']
        type = package['package']['type']
        arch = package['package']['arch']
        if name == 'c':
            generate_sysroot(package, context)
            print('Generated sysroot')
            continue
        if name in SYSROOT_PACKAGES:
            print('Ignoring sysroot part: %s' % name)
            continue
        if type == 'tool':
            generate_host_tool(package, context)
        elif type == 'lib':
            if arch == 'src':
                type = 'source'
                generate_source_library(package, context)
            else:
                type = 'prebuilt'
                generate_compiled_library(package, context)
        elif type == 'fidl':
            generate_fidl_library(package, context)
        else:
            print('(%s) Unsupported package type: %s/%s, skipping'
                  % (name, type, arch))
            continue
        if debug:
            print('Processed %s (%s)' % (name, type))


if __name__ == '__main__':
    sys.exit(main())
