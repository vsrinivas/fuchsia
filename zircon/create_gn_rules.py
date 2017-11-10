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
import tempfile


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # build
    SCRIPT_DIR))                 # zircon
ZIRCON_ROOT = os.path.join(FUCHSIA_ROOT, 'zircon')

sys.path += [os.path.join(FUCHSIA_ROOT, "third_party", "mako")]
from mako.template import Template


def make_dir(path, is_dir=False):
    """Creates the directory at `path`."""
    target = path if is_dir else os.path.dirname(path)
    try:
        os.makedirs(target)
    except OSError as exception:
        if exception.errno == errno.EEXIST and os.path.isdir(target):
            pass
        else:
            raise


def try_remove(list, element):
    """Attempts to remove an element from a list, returning `true` if
       successful."""
    try:
        list.remove(element)
        return True
    except ValueError:
        return False


def parse_package(lines):
    """Parses the content of a package file."""
    result = {}
    section_exp = re.compile('^\[([^\]]+)\]$')
    attr_exp = re.compile('^([^=]+)=(.*)$')
    current_section = None
    def finalize_section():
        if not current_section:
            return
        if current_list and current_map:
            raise Error('Found both map-style and list-style section')
        result[current_section] = (current_map if current_map
                                   else current_list)
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


class SourceLibrary(object):
    """Represents a library built from sources.

       Convenience storage object to be consumed by Mako templates."""

    def __init__(self, name):
        self.name = name
        self.include_dirs = set()
        self.sources = []
        self.deps = []
        self.libs = set()



def generate_source_library(package, out_dir):
    """Generates the build glue for the given library."""
    lib_name = package['package']['name']
    data = SourceLibrary(lib_name)
    source_exp = re.compile('^SOURCE')

    # Includes.
    for name, path in package['includes'].iteritems():
        # name: foo/bar.h
        # path: SOURCE/somewhere/under/zircon/foo/bar.h
        (full_path, _) = re.subn(source_exp, ZIRCON_ROOT, path)
        data.sources.append('//%s' % os.path.relpath(full_path, FUCHSIA_ROOT))
        if full_path.endswith(name):
            include = os.path.relpath(full_path[:-len(name)], FUCHSIA_ROOT)
            data.include_dirs.add('//%s' % include)
        else:
            raise Exception(
                    '(%s) %s not found within %s' % (lib_name, name, full_path))

    # Source files.
    for name, path in package['src'].iteritems():
        # name: foo.cpp
        # path: SOURCE/somewhere/under/zircon/foo.cpp
        (file, _) = re.subn(source_exp, ZIRCON_ROOT, path)
        data.sources.append('//%s' % os.path.relpath(full_path, FUCHSIA_ROOT))

    # Dependencies.
    data.deps.extend(package['deps'])
    try_remove(data.deps, 'c')
    if try_remove(data.deps, 'zircon'):
        data.libs.add('zircon')

    # Generate the build file.
    build_path = os.path.join(out_dir, 'lib', lib_name, 'BUILD.gn')
    make_dir(build_path)
    template_path = os.path.join(SCRIPT_DIR, 'source_library.mako')
    contents = Template(filename=template_path).render(data=data)
    with open(build_path, 'w') as build_file:
        build_file.write(contents)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out',
                        help='Path to the output directory',
                        required=True)
    parser.add_argument('--debug',
                        help='Whether to print out debug information',
                        action='store_true')
    args = parser.parse_args()

    out_dir = os.path.abspath(args.out)
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    debug = args.debug

    # Generate package descriptions through Zircon's build.
    zircon_dir = tempfile.mkdtemp('-zircon-packages')
    if debug:
        print('Building Zircon in: %s' % zircon_dir)
    make_args = [
        'make',
        'packages',
        'BUILDDIR=%s' % zircon_dir,
    ]
    subprocess.check_call(make_args, cwd=ZIRCON_ROOT,
                          env={} if debug else {'QUIET': '1'})

    # Parse package definitions.
    packages = []
    for root, dirs, files in os.walk(os.path.join(zircon_dir, 'export')):
        for file in files:
            (path, ext) = os.path.splitext(file)
            if not ext == '.pkg':
                continue
            with open(os.path.join(root, file), 'r') as pkg_file:
                packages.append(parse_package(pkg_file.readlines()))
        break
    if debug:
        print('Found %s packages:' % len(packages))
        names = sorted(map(lambda p: p['package']['name'], packages))
        for name in names:
            print(' - %s' % name)

    if not debug:
        shutil.rmtree(zircon_dir)

    # Generate some GN glue for each package.
    for package in packages:
        name = package['package']['name']
        type = package['package']['type']
        arch = package['package']['arch']
        if type != 'lib' and arch != 'src':
            print('(%s) Unsupported package type: %s/%s, skipping'
                  % (name, type, arch))
        generate_source_library(package, out_dir)


if __name__ == "__main__":
    sys.exit(main())
