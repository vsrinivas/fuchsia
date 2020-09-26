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
import os
import sys

# These must be fully qualified labels (without a toolchain).
ALLOWED_CTS_DEPS = [
    '//sdk:sdk',
    '//zircon/public/lib/zxtest:zxtest',
]

CTS_EXTENSION = '.this_is_cts'


class VerifyCtsDeps:
    """Helper class to verify GN dependencies in CTS.

    Args:
      root_build_dir (string): An absolute path to the GN's $root_build_dir.
      cts_file (string): The path to (including) the file to be generated.
      invoker_label (string): The label of the invoker of cts_element.
      deps (list(string)): A list of fully qualified GN labels.

    Raises: ValueError if any parameter is empty or if root_build_dir does not exist.
    """

    def __init__(self, root_build_dir, cts_file, invoker_label, deps):
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
            if dep not in ALLOWED_CTS_DEPS and not os.path.exists(
                    self.get_file_path(dep)):
                unaccepted_deps.append(dep)

        return unaccepted_deps

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
    args = parser.parse_args()
    try:
        cts_element = VerifyCtsDeps(
            args.root_build_dir, args.output, args.invoker_label, args.deps)
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
