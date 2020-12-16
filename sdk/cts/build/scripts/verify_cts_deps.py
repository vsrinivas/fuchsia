#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A helper script for CTS dependency verification.

The Compatibility Test Suite (CTS) has strict rules on GN dependencies. A CTS target may only depend
on other CTS targets and carefully selected dependencies listed in ALLOWED_CTS_DEPS.

The CTS creates a CTS file for each CTS target at GN gen time. This CTS file is used to indicate
that a target is acceptable in CTS. Each CTS target verifies its own dependencies using this script.
If a dependency does not have a CTS file, the script will look for that fully qualified dependency
in ALLOWED_CTS_DEPS. If it does have a CTS file, verification of that target will be deferred such
that the target may verify its own dependencies.
"""

import argparse
import json
import os
import re
import sys

CTS_EXTENSION = '.this_is_cts'


class VerifyCtsDeps:
    """Helper class to verify GN dependencies in CTS.

    Args:
      root_build_dir (string): An absolute path to the GN's $root_build_dir.
      cts_file (string): The path to (including) the file to be generated.
      invoker_label (string): The label of the invoker of cts_element.
      deps (list(string)): A list of fully qualified GN labels.
      allowed_cts_deps(list(string)): A list of allowed deps found in //sdk/cts/allowed_cts_deps.gni"
      allowed_cts_dirs(list(string)): A list of allowed directories found in //sdk/cts/allowed_cts_dirs.gni"
      sdk_manifests(list(string)): A list of absolute paths to SDK manifest files.

    Raises: ValueError if any parameter is empty, if root_build_dir does not exist, or if the sdk_manifests do not exist.
    """

    def __init__(self, root_build_dir, cts_file, invoker_label, deps,
                 allowed_cts_deps, allowed_cts_dirs, sdk_manifests):
        if root_build_dir and os.path.exists(root_build_dir):
            self.root_build_dir = root_build_dir
        else:
            raise ValueError('root_build_dir cannot be empty and must exist.')

        if cts_file:
            self.cts_file = cts_file
        else:
            raise ValueError('cts_file cannot be empty.')

        if invoker_label:
            self.invoker_label = invoker_label
        else:
            raise ValueError('invoker_label cannot be empty.')

        if deps:
            self.deps = deps
        else:
            raise ValueError('deps cannot be empty.')

        if allowed_cts_deps:
            self.allowed_cts_deps = allowed_cts_deps
        else:
            raise ValueError('allowed_cts_deps cannot be empty')

        if allowed_cts_dirs:
            self.allowed_cts_dirs = allowed_cts_dirs
        else:
            raise ValueError('allowed_cts_dirs cannot be empty')

        if sdk_manifests:
            for manifest in sdk_manifests:
                if not os.path.isfile(manifest):
                    raise ValueError('manifest %s does not exist' % manifest)
            self.sdk_manifests = sdk_manifests
        else:
            raise ValueError('sdk_manifests cannot be empty')

    def get_file_path(self, dep):
        """Returns the path to a CTS file.

        Args:
          dep (string): A GN label.

        Returns:
          A string containing the absolute path to the target's CTS file.
        """

        # Remove the leading slashes from the label.
        dep = dep[2:]

        # Get the target_name from the label.
        if ':' in dep:
            # zircon/public/lib/zxtest:zxtest
            dep, target_name = dep.split(':')
        elif '/' in dep:
            # zircon/public/lib/zxtest
            _, target_name = dep.rsplit('/', 1)
        else:
            # sdk
            target_name = dep

        return self.root_build_dir + '/cts/' + dep + '/' + target_name + CTS_EXTENSION

    def verify_deps(self):
        """Verifies the element's dependencies are allowed in CTS.

        CTS has strict dependency rules. All dependencies must be in ALLOWED_CTS_DEPS or be CTS
        targets themselves.

        Returns:
          A list of labels that are unaccepted in CTS. The list will be empty if all deps are allowed.
        """
        unaccepted_deps = []
        for dep in self.deps:
            dep_found = False

            if dep in self.allowed_cts_deps or os.path.exists(
                    self.get_file_path(dep)):
                dep_found = True
            else:
                # Dep isn't in the allow list and a CTS file doesn't exist. Check if
                # all targets in dep's directory are allowed (//third_party/dart-pkg/pub/*).
                for allowed_dir in self.allowed_cts_dirs:
                    pattern = re.compile(allowed_dir)
                    if pattern.match(dep):
                        dep_found = True

                # Check if dep is an SDK target.
                if not dep_found:
                    dep_found = self.verify_dep_in_sdk(dep)

            if not dep_found:
                unaccepted_deps.append(dep)

        return unaccepted_deps

    def verify_dep_in_sdk(self, dep):
        """Verifies that a dependency is released in an SDK.

        Looks for the dependency in the the list of SDK manifests.

        Returns:
            A boolean determining whether the dependency is released in an SDK.
        """

        sdk_atom_labels = {}
        for sdk_manifest in self.sdk_manifests:
            try:
                with open(sdk_manifest, 'r') as manifest:
                    data = json.load(manifest)
            except json.JSONDecodeError:
                continue
            for atom in data['atoms']:
                # SDK atoms are appended with one of the following and a
                # toolchain, so we want to ignore them to match against the
                # provided label.
                match_label = re.compile("(?:(_sdk)|(_sdk_manifest)|(_sdk_legacy))\(.*\)")
                label = re.sub(match_label, '', atom['gn-label'])
                sdk_atom_labels[label] = atom['category']

        return dep in sdk_atom_labels and sdk_atom_labels[dep] in [ 'partner', 'public' ]

    def create_cts_dep_file(self):
        """Create a CTS file containing the verified dependencies.

        Should only be run after dependencies are verified using verify_deps.
        """
        target_gen_dir, _ = self.cts_file.rsplit('/', 1)
        if not os.path.exists(target_gen_dir):
            os.makedirs(target_gen_dir)

        with open(self.cts_file, 'w') as f:
            for dep in self.deps:
                f.write('%s\n' % dep)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--root_build_dir',
        required=True,
        help='Path to the GN $root_build_dir. The default is //out/default.')
    parser.add_argument(
        '--output',
        required=True,
        help='The path to (including) the file to be generated.')
    parser.add_argument(
        '--invoker_label',
        required=True,
        help='The label of the invoker of cts_element.')
    parser.add_argument(
        '--deps',
        nargs='+',
        required=True,
        help=
        'A list of at least one GN label representing the target\'s dependencies.'
    )
    parser.add_argument(
        '--allowed_cts_deps',
        nargs='+',
        required=True,
        help='The list of allowed CTS dependencies in allowed_cts_deps.gni')
    parser.add_argument(
        '--allowed_cts_dirs',
        nargs='+',
        required=True,
        help=
        'The list of allowed CTS dependency directories in allowed_cts_deps.gni'
    )
    parser.add_argument(
        '--sdk_manifests',
        nargs='+',
        required=True,
        help='The list of paths to public and partner SDK manifests')
    args = parser.parse_args()
    try:
        cts_element = VerifyCtsDeps(
            args.root_build_dir, args.output, args.invoker_label, args.deps,
            args.allowed_cts_deps, args.allowed_cts_dirs, args.sdk_manifests)
    except ValueError as e:
        print('ValueError: %s' % e)
        return 1

    unaccepted_deps = cts_element.verify_deps()
    if not unaccepted_deps:
        cts_element.create_cts_dep_file()
    else:
        print(
            'The following dependencies of "%s" are not allowed in CTS: %s' %
            (args.invoker_label, unaccepted_deps))
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
