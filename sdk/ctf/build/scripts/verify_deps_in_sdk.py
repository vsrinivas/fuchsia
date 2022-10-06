#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A helper script for verifying dependencies are released in an SDK.

This script is intended to run as a build time check to make sure that dependencies
are released in a "public" or "partner" SDK. This is to ensure that a target depends
on stable API / ABI.
"""

import argparse
import json
import os
import re
import sys

CTF_EXTENSION = '.this_is_ctf'


class VerifyDepsInSDK:
    """Helper class to verify GN dependencies are released in a public SDK.

    Args:
      root_build_dir (string): An absolute path to the GN's $root_build_dir.
      output_file (string): The path to (including) the file to be generated.
      invoker_label (string): The label of the invoker of verify_deps_in_sdk.
      deps_to_verify (list(string)): A list of fully qualified GN labels.
      allowed_deps (list(string)): A list of allowed deps found in //sdk/ctf/allowed_ctf_deps.gni"
      allowed_dirs (list(string)): A list of allowed directories found in //sdk/ctf/allowed_ctf_deps.gni"
      sdk_manifests (list(string)): A list of absolute paths to SDK manifest files.

    Raises:
      ValueError if any parameter is empty, if root_build_dir does not exist, or if the sdk_manifests do not exist.
    """

    def __init__(
            self, root_build_dir, output_file, invoker_label, deps_to_verify,
            allowed_deps, allowed_dirs, sdk_manifests):
        if not (root_build_dir and os.path.exists(root_build_dir)):
            raise ValueError('root_build_dir cannot be empty and must exist.')

        if not output_file:
            raise ValueError('output_file cannot be empty.')

        if not invoker_label:
            raise ValueError('invoker_label cannot be empty.')

        if not deps_to_verify:
            raise ValueError('deps_to_verify cannot be empty.')

        if not allowed_deps:
            raise ValueError('allowed_deps cannot be empty')

        if not allowed_dirs:
            raise ValueError('allowed_dirs cannot be empty')

        if sdk_manifests:
            for manifest in sdk_manifests:
                if not os.path.isfile(manifest):
                    raise ValueError(f'manifest {manifest} does not exist')
        else:
            raise ValueError('sdk_manifests cannot be empty')

        self.root_build_dir = root_build_dir
        self.output_file = output_file
        self.invoker_label = invoker_label
        self.deps_to_verify = deps_to_verify
        self.allowed_deps = allowed_deps
        self.allowed_dirs = allowed_dirs
        self.sdk_manifests = sdk_manifests

    def get_ctf_file_path(self, dep):
        """Returns the path to a CTF file.

        The CTF creates a file for each CTF target at GN gen time. This CTF file is used to indicate
        that a target is acceptable in CTF. Each CTF target verifies its own dependencies using this script.
        If a dependency does not have a CTF file, the script will look for that fully qualified dependency
        in allowed_deps. If it does have a CTF file, verification of that target will be deferred such
        that the target may verify its own dependencies.

        Args:
          dep (string): A GN label.

        Returns:
          A string containing the absolute path to the target's CTF file.
        """

        # Remove the leading slashes from the label.
        dep = dep[2:]

        # Get the target_name from the label.
        if ':' in dep:
            # path/to/lib:lib
            dep, target_name = dep.split(':')
        elif '/' in dep:
            # path/to/lib
            _, target_name = dep.rsplit('/', 1)
        else:
            # lib
            target_name = dep

        return self.root_build_dir + '/ctf/' + dep + '/' + target_name + CTF_EXTENSION

    def verify_deps(self):
        """Verifies the element's dependencies are released in a public SDK.

        All dependencies must be in allowed_deps, allowed_dirs, or be CTF targets themselves.

        Note: At the time this script runs, these dependencies (self.deps_to_verify)
        have not necessarily been built. The verification target does not depend on them.

        Returns:
          A list of labels that are not allowed to be used. The list will be empty if all deps are allowed.
        """
        unaccepted_deps = []
        unverified_deps = []
        for dep in self.deps_to_verify:
            # Removes FIDL binding suffixes because the SDK manifests will only
            # contain the FIDL name.
            for suffix in ["_hlcpp", "_rust"]:
                if dep.endswith(suffix):
                    dep = dep[:-len(suffix)]
                    break

            dep_found = False
            if dep in self.allowed_deps or os.path.exists(
                    self.get_ctf_file_path(dep)):
                dep_found = True
            else:
                # Dep isn't in the allow list and a CTF file doesn't exist. Check if
                # all targets in dep's directory are allowed (//third_party/dart-pkg/pub/*).
                for allowed_dir in self.allowed_dirs:
                    pattern = re.compile(allowed_dir)
                    if pattern.match(dep):
                        dep_found = True

                # Check if dep is an SDK target.
                if not dep_found:
                    unverified_deps.append(dep)

        if len(unverified_deps) > 0:
            unaccepted_deps = self.verify_deps_in_sdk(unverified_deps)

        return unaccepted_deps

    def verify_deps_in_sdk(self, deps):
        """Verifies that dependencies are released in an SDK.

        Returns:
            The list of dependencies that are not released in an SDK.
        """
        # SDK atoms are appended with one of the following and a
        # toolchain, so we want to ignore them to match against the
        # provided label.
        match_label = re.compile(
            "(?:(_sdk)|(_sdk_manifest)|(_sdk_legacy))\(.*\)")

        sdk_atom_label_to_category = {}
        for sdk_manifest in self.sdk_manifests:
            try:
                with open(sdk_manifest, 'r') as manifest:
                    data = json.load(manifest)
            except json.JSONDecodeError:
                continue
            for atom in data['atoms']:
                label = re.sub(match_label, '', atom['gn-label'])
                sdk_atom_label_to_category[label] = atom['category']

        unaccepted_deps = []
        for dep in deps:
            if dep not in sdk_atom_label_to_category or sdk_atom_label_to_category[
                    dep] not in ['partner', 'public']:
                unaccepted_deps.append(dep)

        return unaccepted_deps

    def create_output_file(self):
        """Create an output file containing the verified dependencies.

        Should only be run after dependencies are verified using verify_deps.
        """
        target_gen_dir, _ = self.output_file.rsplit('/', 1)
        if not os.path.exists(target_gen_dir):
            os.makedirs(target_gen_dir)

        with open(self.output_file, 'w') as f:
            for dep in self.deps_to_verify:
                f.write(f'{dep}\n')


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
        help='The label of the invoker of verify_deps_in_sdk.')
    parser.add_argument(
        '--deps_to_verify',
        nargs='+',
        required=True,
        help=
        'A list of at least one GN label representing the target\'s dependencies.'
    )
    parser.add_argument(
        '--allowed_deps',
        nargs='+',
        required=True,
        help='A list of allowed dependencies.')
    parser.add_argument(
        '--allowed_dirs',
        nargs='+',
        required=True,
        help=
        'A list of directories where any dependency is allowed. Directories should end in /*'
    )
    parser.add_argument(
        '--sdk_manifests',
        nargs='+',
        required=True,
        help='The list of paths to public and partner SDK manifests')
    args = parser.parse_args()
    try:
        target = VerifyDepsInSDK(
            args.root_build_dir, args.output, args.invoker_label,
            args.deps_to_verify, args.allowed_deps, args.allowed_dirs,
            args.sdk_manifests)
    except ValueError as e:
        print(f'ValueError: {e}')
        return 1

    unaccepted_deps = target.verify_deps()
    if not unaccepted_deps:
        target.create_output_file()
    else:
        print(
            f'The following dependencies of "{args.invoker_label}" are not allowed: {unaccepted_deps}'
        )
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
