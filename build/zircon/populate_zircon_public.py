#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import shutil
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # build
        SCRIPT_DIR))                 # zircon
ZIRCON_PUBLIC = os.path.join(FUCHSIA_ROOT, 'zircon', 'public')
EXPORT_TEMPLATE_FILE = os.path.join(SCRIPT_DIR, 'template.gn')
UNIFICATION_DIR = os.path.join(FUCHSIA_ROOT, 'build', 'unification')
MAPPINGS_FILE = os.path.join(UNIFICATION_DIR, 'zircon_library_mappings.json')
FORWARD_TEMPLATE_FILE = os.path.join(UNIFICATION_DIR,
                                     'zircon_library_forward.gn')

DIRS = {
    'lib': True,
    'tool': False,
}
PUBLIC_DIRS = set(DIRS.keys())

MARKER = 'ONLY EDIT IT BY THAT NAME!'


def is_template(build_file):
    with open(build_file, 'r') as file:
        return MARKER in file.read()


def has_sources(top_dir):
    return DIRS[top_dir]


def main():
    with open(sys.argv[1]) as f:
        legacy_dirs = json.load(f)
    with open(MAPPINGS_FILE, 'r') as f:
        content = json.load(f)
        mapped_lib_dirs = dict([('lib/' + i['name'], i['label'])
                                for i in content])

    # Verify that we're not trying to create a forwarding target and an exported
    # library under the same alias.
    common_dirs = set(mapped_lib_dirs) & set(legacy_dirs)
    if common_dirs:
        print('The following paths cannot be both exports from Zircon and '
              'forwarding targets:')
        for dir in common_dirs:
            print('//zircon/public/' + dir)
        return 1

    # Create a data structure holding all generated paths.
    all_dirs = {}
    for dir in legacy_dirs:
        all_dirs[dir] = EXPORT_TEMPLATE_FILE
    for dir in mapped_lib_dirs:
        all_dirs[dir] = FORWARD_TEMPLATE_FILE

    dirs = {}
    for dir, template in all_dirs.iteritems():
        top_dir = os.path.dirname(dir)
        name = os.path.basename(dir)
        subdirs, templates = dirs.setdefault(top_dir, ([], {}))
        templates[name] = template
        dirs[top_dir] = (subdirs + [name], templates)
    assert set(dirs.keys()).issubset(PUBLIC_DIRS), (
        "%r from JSON should be a subset of %r" %
        (set(dirs.keys()), PUBLIC_DIRS))
    stats = dict([(f, os.lstat(f))
                  for f in [EXPORT_TEMPLATE_FILE, FORWARD_TEMPLATE_FILE]])
    for top_dir in dirs:
        subdirs, templates = dirs[top_dir]
        top_dir_name = top_dir
        top_dir = os.path.join(ZIRCON_PUBLIC, top_dir)
        subdirs = set(subdirs)
        if not os.path.exists(top_dir):
            os.mkdir(top_dir)
        else:
            # Go over the existing contents of the directory.
            for existing in os.listdir(top_dir):
                existing_dir = os.path.join(top_dir, existing)
                if not os.path.isdir(existing_dir):
                    # Disregard files (e.g. .gitignore).
                    continue
                build_file = os.path.join(existing_dir, 'BUILD.gn')
                is_source = (has_sources(top_dir_name) and
                             os.path.exists(build_file) and
                             not is_template(build_file))
                if existing in subdirs:
                    if is_source:
                        print('%s cannot be both a source and generated' %
                              existing_dir)
                        return 1
                    # An existing directory might already have the link.
                    # If the link doesn't exist or doesn't match, make it.
                    template = templates[existing]
                    if not os.path.exists(build_file):
                        os.link(template, build_file)
                    elif not os.path.samestat(os.lstat(build_file),
                                              stats[template]):
                        os.remove(build_file)
                        os.link(template, build_file)
                    subdirs.remove(existing)
                else:
                    if not is_source:
                        # A stale directory that shouldn't exist any more.
                        shutil.rmtree(existing_dir)
        # Make and populate any directories that don't exist yet.
        for subdir in subdirs:
            template = templates[subdir]
            subdir = os.path.join(top_dir, subdir)
            os.mkdir(subdir)
            os.link(template, os.path.join(subdir, 'BUILD.gn'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
