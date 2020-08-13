#!/usr/bin/env python2.7

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import platform
import os.path
import subprocess
import sys

HOST_PLATFORM = "%s-%s" % (
    platform.system().lower().replace("darwin", "mac"),
    {
        "x86_64": "x64",
        "aarch64": "arm64",
    }[platform.machine()],
)

fuchsia_root = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
sys.path.append(os.path.join(fuchsia_root, 'src'))

from area_dependency_exceptions import exceptions

allowed_deps = [
    # These dependencies are always allowed:
    # https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/source_code/layout.md#dependency-structure
    '//build',
    '//prebuilt',
    '//sdk',
    '//third_party',

    # The follow entries are temporarily allowed universally, but should be
    # moved to //sdk or //src/lib:
    # Code libraries
    '//garnet/lib/rust',
    '//garnet/public/lib',
    '//garnet/public/rust',
    '//zircon/public/fidl',
    '//zircon/public/lib',
    '//zircon/public/banjo',
    '//zircon/system/fidl',
    '//zircon/system/public',

    # Tools
    '//tools',
    # Will move to //tools or //sdk:
    '//garnet/go/src/fidlmerge:fidlmerge(//build/toolchain:host_x64)',
    '//garnet/go/src/pm:pm_bin(//build/toolchain:host_x64)',
    '//topaz/bin/fidlgen_dart:fidlgen_dart(//build/toolchain:host_x64)',
    '//zircon/public/tool',

    # This is currently implicitly generated as a dependency on any C++
    # generation of a FIDL target.
    # TODO(ctiller): File an issue for cleaning this up.
    '//src/connectivity/overnet/lib/protocol:fidl_stream',
    '//src/connectivity/overnet/lib/embedded:runtime',
]

target_types_to_check = [
    'action',
    'executable',
    'loadable_module',
    'shared_library',
    'source_set',
    'static_library',
]


class DisallowedDepsRecord:

    def __init__(self):
        self.count = 0
        self.labels = {}

    def AddBadDep(self, label, dep):
        self.count = self.count + 1
        if label not in self.labels:
            self.labels[label] = []
        self.labels[label].append(dep)


# An area is defined as a directory within //src/ that contains an OWNERS file.
# See docs/development/source_code/layout.md
def area_for_label(source_dir, label):
    src_prefix = '//src/'
    if not label.startswith(src_prefix):
        return ''  # Not in an area
    if ':' in label:
        label = label[0:label.find(':')]
    while label != '//':
        expected_owners_path = os.path.join(source_dir, label[2:], 'OWNERS')
        if os.path.exists(expected_owners_path):
            return label
        label = os.path.dirname(label)
    return ''


# Checks dependency rules as described in
# docs/development/source_code/layout.md#dependency-structure
def dep_allowed(label, label_area, dep, dep_area, testonly, ignore_exceptions):
    # Targets can depend on globally allowed targets
    for a in allowed_deps:
        if dep.startswith(a):
            return True
    # Targets within an area can depend on other targets in the same area
    if label_area == dep_area:
        return True
    # Targets can depend on '//(../*)lib/'
    # Targets marked testonly can depend on '//(../*)testing/'
    while label != '//':
        if dep.startswith(label + '/lib/'):
            return True
        if testonly and dep.startswith(label + '/testing'):
            return True
        label = os.path.dirname(label)
    if ignore_exceptions:
        return False
    # Some areas are temporarily allowed additional dependencies
    if label_area in exceptions:
        prefixes = exceptions[label_area]
        for prefix in prefixes:
            if dep.startswith(prefix):
                return True
    return False


def record_bad_dep(bad_deps, area, label, bad_dep):
    if area not in bad_deps:
        bad_deps[area] = DisallowedDepsRecord()
    bad_deps[area].AddBadDep(label, bad_dep)


def extract_build_graph(gn_binary, out_dir):
    args = [
        gn_binary, 'desc', out_dir, '//src/*', '--format=json',
        '--all-toolchains'
    ]
    json_build_graph = subprocess.check_output(args)
    return json.loads(json_build_graph)


def main():
    parser = argparse.ArgumentParser(
        description='Check dependency graph in areas')
    parser.add_argument(
        '--out', default='out/default', help='Build output directory')
    parser.add_argument(
        '--ignore-exceptions',
        action='store_true',
        help='Ignore registered exceptions.  ' +
        'Set to see all dependency issues')
    args = parser.parse_args()

    gn_binary = os.path.join(
        fuchsia_root, 'prebuilt', 'third_party', 'gn', HOST_PLATFORM, 'gn')
    targets = extract_build_graph(
        gn_binary, os.path.join(fuchsia_root, args.out))

    disallowed_dependencies = {}
    for label, target in targets.iteritems():
        if target['type'] not in target_types_to_check:
            continue
        label_area = area_for_label(fuchsia_root, label)
        testonly = target['testonly']
        for dep in target['deps']:
            dep_area = area_for_label(fuchsia_root, dep)
            if not dep_allowed(label, label_area, dep, dep_area, testonly,
                               args.ignore_exceptions):
                record_bad_dep(disallowed_dependencies, label_area, label, dep)

    total_count = 0
    for area in sorted(disallowed_dependencies.keys()):
        disallowed_deps = disallowed_dependencies[area]
        print 'Area %s has %d disallowed dependencies:' % (
            area, disallowed_deps.count)
        total_count = total_count + disallowed_deps.count

        for label in sorted(disallowed_deps.labels.keys()):
            bad_deps = disallowed_deps.labels[label]
            print '  Target %s has %d disallowed dependencies:' % (
                label, len(bad_deps))
            for dep in sorted(bad_deps):
                print '    %s' % dep
            print
        print

    print 'Found %d dependency errors' % total_count
    if total_count != 0:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
