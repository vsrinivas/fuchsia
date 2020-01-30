#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""SDK Frontend generator for transforming the IDK into the GN SDK.

This class accepts a directory or tarball of an instance of the Integrator
Developer Kit (IDK) and uses the metadata from the IDK to drive the
construction of a specific SDK for use in projects using GN.
"""

import argparse
import os
import shutil
import stat
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(  # scripts
        os.path.dirname(  # sdk
            SCRIPT_DIR)))  # gn

sys.path += [os.path.join(FUCHSIA_ROOT, 'scripts', 'sdk', 'common')]
from frontend import Frontend


class GNBuilder(Frontend):
    """Frontend for GN.

  Based on the metadata in the IDK used, this frontend generates GN
  specific build rules for using the contents of the IDK. It also adds a
  collection of tools for inteacting with devices and emulators running
  Fuchsia.

  The templates used are in the templates directory, and the static content that
  is added to the SDK is in the base directory.
  """

    def __init__(self, local_dir, output, archive='', directory=''):
        """Initializes an instance of the GNBuilder.

        Note that only one of archive or directory should be specified.

    Args:
      local_dir: The local directory used to find additional resources used
        during the transformation.
     output: The output directory. The contents of this directory are removed
       before processing the IDK.
      archive: The tarball archive to process. Can be empty meaning use the
        directory parameter as input.
      directory: The directory containing an unpackaged IDK. Can be empty
        meaning use the archive parameter as input.
    """
        super(GNBuilder, self).__init__(
            local_dir=local_dir,
            output=output,
            archive=archive,
            directory=directory)
        self.target_arches = []
        self.fidl_targets = [] # List of all FIDL library targets generated

    def prepare(self, arch, types):
        """Called before elements are processed.

    This is called by the base class before the elements in the IDK are
    processed.
    At this time, the contents of the 'base' directory are copied to the
    output directory, as well as the 'meta' directory from the IDK.

    Args:
      arch: A json object describing the host and target architectures.
      types: The list of atom types contained in the metadata.
    """
        del types  # types is not used.
        self.target_arches = arch['target']

        # Copy the common files. The destination of these files is relative to the
        # root of the fuchsia_sdk.
        shutil.copytree(self.local('base'), self.output)

        # Propagate the metadata for the Core SDK into the GN SDK.
        shutil.copytree(self.source('meta'), self.dest('meta'))

    def finalize(self, arch, types):
        self.write_top_level_files()

    def write_top_level_files(self):
        self.write_file(self.dest('.gitignore'), 'gitignore', self)
        self.write_file(self.dest('BUILD.gn'), 'top_level_build', self)


class TestData(object):
    """Class representing test data to be added to the run_py mako template"""

    def __init__(self):
        self.fuchsia_root = FUCHSIA_ROOT

def make_executable(path):
    st = os.stat(path)
    os.chmod(path, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

def create_test_workspace(output):
    # Remove any existing output.
    shutil.rmtree(output, True)
    # Copy the base tests.
    shutil.copytree(
        os.path.join(SCRIPT_DIR, 'test_project'), output)
    # run.py file
    builder = Frontend(local_dir=SCRIPT_DIR)
    run_py_path = os.path.join(output, 'run.py')
    builder.write_file(
        path=run_py_path, template_name='run_py', data=TestData())
    make_executable(run_py_path)

    return True


def main():
    parser = argparse.ArgumentParser(
        description='Creates a GN SDK for a given SDK tarball.')
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument(
        '--archive', help='Path to the SDK archive to ingest', default='')
    source_group.add_argument(
        '--directory', help='Path to the SDK directory to ingest', default='')
    parser.add_argument(
        '--output',
        help='Path to the directory where to install the SDK',
        required=True)
    parser.add_argument(
        '--tests', help='Path to the directory where to generate tests')
    args = parser.parse_args()

    return run_generator(
        archive=args.archive,
        directory=args.directory,
        output=args.output,
        tests=args.tests)

def run_generator(archive, directory, output, tests=''):
    """Run the generator. Returns 0 on success, non-zero otherwise.

    Note that only one of archive or directory should be specified.

    Args:
        archive: Path to an SDK tarball archive.
        directory: Path to an unpackaged SDK.
        output: The output directory.
        tests: The directory where to generate tests. Defaults to empty string.
    """

    # Remove any existing output.
    shutil.rmtree(output)

    builder = GNBuilder(
        archive=archive,
        directory=directory,
        output=output,
        local_dir=SCRIPT_DIR)
    if not builder.run():
        return 1

    if tests:
        # Create the tests workspace
        if not create_test_workspace(tests):
            return 1
        # Copy the GN SDK to the test workspace
        wrkspc_sdk_dir = os.path.join(tests, 'third_party', 'fuchsia-sdk')
        shutil.copytree(output, wrkspc_sdk_dir)

    return 0


if __name__ == '__main__':
    sys.exit(main())
