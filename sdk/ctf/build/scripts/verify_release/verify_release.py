#!/usr/bin/env python3
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import unittest
import os
import re
import shutil
import sys
import subprocess
import pathlib
import shutil
import json


class CTS:
    """
    Generate a CTS and release it into the fuchsia //prebuilt directory.

    This script can be used to validate that the current git workspace is capable
    of successfully generating a CTS with passing tests.
    """

    def __init__(
            self,
            fuchsia_dir=os.environ['FUCHSIA_DIR'],
            out_dir="out/default",
            cts_version="test",
            product="core",
            run_tests=False,
            board="x64"):

        self.fuchsia_dir = fuchsia_dir
        self.out_dir = "{}/{}".format(self.fuchsia_dir, out_dir)
        self.cts_version = cts_version
        self.product = product
        self.board = board
        self.run_tests = run_tests

        self.release_dir = "{}/prebuilt/cts/{}".format(
            self.fuchsia_dir, self.cts_version)
        self.ctf_artifacts = "{}/ctf_artifacts.json".format(self.out_dir)

    def _print(self, string, end='\n'):
        print(string, end=end)
        sys.stdout.flush()

    def _check_output(self, cmd):
        try:
            output = subprocess.check_output(
                cmd, stderr=subprocess.STDOUT, shell=True)
        except subprocess.CalledProcessError as exc:
            self._print("FAILED!")
            self._print(" -> {}\n".format(cmd))
            self._print(exc.output.decode("utf-8"))
            sys.exit(1)
        return output.decode("utf-8")

    def _build_cts_from_workspace(self):
        """
        Build the CTS from your current workspace: //sdk/ctf

        Throws an exception on build failure.
        """
        self._print("Building the CTS in your workspace....", end='')

        cmd = "fx set {}.{} --with //sdk/ctf --args 'cts_version=\"{}\"' && fx build".format(
            self.product, self.board, self.cts_version)

        self._check_output(cmd)
        self._print("Done.")

    def _release_cts_to_prebuilt_directory(self):
        """
        Copy the built CTS to the release directory: //prebuilt/cts/<cts_version>

        Throws an exception if any file listed in ctf_artifacts.json does not exist.
        """
        self._print("Releasing the CTS....", end='')

        # Remove previous releases if they exist.
        if os.path.isdir(self.release_dir):
            shutil.rmtree(self.release_dir)

        # Create the release directory.
        pathlib.Path(self.release_dir).mkdir(parents=True, exist_ok=True)

        with open(self.ctf_artifacts, 'r') as ctf_artifacts:
            data = ctf_artifacts.read()
        sdk_files = json.loads(data)

        # Using the ctf_artifacts file, copy every file to the release directory.
        for f in sdk_files:
            src = "{}/{}".format(self.out_dir, f)
            dest = "{}/{}".format(self.release_dir, f)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copyfile(src, dest)

        self._print("Done.")

    def _build_cts_release(self):
        """
        Build the released CTS : //prebuilt/cts/<cts_version>:tests

        Throws an exception if the CTS fails to build.
        """
        self._print("Building the released CTS....", end='')

        # Modify the BUILD.gn file template for the current CTS version.
        build_file_template = os.path.join(
            self.fuchsia_dir, 'sdk', 'cts', 'build', 'scripts',
            'verify_release', '_BUILD.gn')
        with open(build_file_template, 'r') as f:
            build_file = f.read()
        build_file = build_file.replace("{cts_version}", self.cts_version)
        build_file_path = "{}/{}".format(self.release_dir, "BUILD.gn")

        # Write the BUILD.gn file text to the release directory.
        with open(build_file_path, "w") as f:
            f.writelines(build_file)

        # Build the new CTS release.
        cmd = "fx set {}.{} --with //prebuilt/cts/{}:tests && fx build".format(
            self.product, self.board, self.cts_version)

        self._print(cmd)
        self._check_output(cmd)
        self._print("Done.")

    def _run_tests(self):
        """
        Run the tests in the released CTS: //prebuilt/cts/<cts_version>:tests

        Throws an exception if any CTS test fails.
        """
        self._print("Running the released CTS tests....", end='')

        self._print(self._check_output("fx is-package-server-running"))
        self._print(self._check_output("fx test"))

    def run(self):
        if self.run_tests:
            # Error out early if the emulator isn't running.
            self._print("Verifying that fx serve is running...", end='')
            self._check_output("fx is-package-server-running")
            self._print("Done.")

        self._build_cts_from_workspace()
        self._release_cts_to_prebuilt_directory()
        self._build_cts_release()

        self._print(
            "The CTS has been successfully released to {}.".format(
                self.release_dir))

        if self.run_tests:
            self._run_tests()


class VerifyReleaseTests(unittest.TestCase):

    def test_e2e(self):
        # TODO(jcecil): run `fx status --format json` and save the current args.

        try:
            cts = CTS()
            cts.run()
        except Exception as e:
            raise e
        finally:
            # TODO(jcecil): reset the args to how they were initially.
            subprocess.run(
                "fx set core.x64 --with //sdk/ctf/build/scripts:tests ; fx build"
                .split())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--fuchsia_dir",
        default=os.environ['FUCHSIA_DIR'],
        help="Path to the root fuchsia directory.")
    parser.add_argument(
        "--out_dir", default="out/default", help="Path to the out directory.")
    parser.add_argument(
        "--cts_version", default="test", help="CTS version string.")
    parser.add_argument(
        "--run_tests", default=True, help="Run the released CTS tests.")

    args = parser.parse_args()
    args_dict = vars(args)

    if args_dict['fuchsia_dir'] == "":
        raise ValueError(
            "Missing --fuchsia_dir arg, and FUCHSIA_DIR environment variable is empty."
        )

    cts = CTS(**args_dict)
    cts.run()


if __name__ == "__main__":
    if os.getenv("TEST") is not None:
        unittest.main()
    else:
        sys.exit(main())
