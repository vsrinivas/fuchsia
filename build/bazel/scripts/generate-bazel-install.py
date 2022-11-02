#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Create a directory that contains a Bazel binary, its extracted install_base
files as well as a wrapper script named 'bazel'."""

import argparse
import datetime
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

_SCRIPT_DIR = os.path.dirname(__file__)

_INFRA_3PP_GIT_URL = 'https://fuchsia.googlesource.com/infra/3pp'
_INFRA_3PP_GIT_COMMIT = '70a9ea9b301f158a76c1cd31ecac359d2cbaf515'


class OutputTree(object):

    def __init__(self, dst_dir, exist_ok=False):
        self._dst_dir = os.path.abspath(dst_dir)
        self._exist_ok = exist_ok
        os.makedirs(dst_dir, exist_ok=exist_ok)

    def add_file(self, src_path, dst_path):
        copy_dst = os.path.join(self._dst_dir, dst_path)

        def do_copy():
            shutil.copy2(src_path, copy_dst)

        try:
            do_copy()
        except PermissionError as e:
            os.unlink(copy_dst)
            do_copy()

    def add_tree(self, src_path, dst_path):
        copy_dst = os.path.join(self._dst_dir, dst_path)

        def do_copy():
            shutil.copytree(
                src_path,
                copy_dst,
                symlinks=True,
                dirs_exist_ok=self._exist_ok,
            )

        try:
            do_copy()
        except shutil.Error:
            # NOTE: On error, shutil.copytree() raises an shutil.Error
            # with a list of reasons, and filterting on PermissionError is
            # really hard, so just try again after removing the tree.
            shutil.rmtree(copy_dst)
            do_copy()

    def close(self):
        pass


class OutputArchive(object):

    def __init__(self, output_path):
        self._zip = zipfile.ZipFile(
            output_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9)

    def add_file(self, src_path, dst_path):
        self._zip.write(src_path, dst_path)

    def add_tree(self, src_path, dst_path):
        src_path = os.path.abspath(src_path)
        for root, subdirs, filenames in os.walk(src_path):
            dst_subdir = os.path.join(dst_path, os.path.relpath(root, src_path))
            for filename in filenames:
                dst_path = os.path.join(dst_subdir, filename)
                src_path = os.path.join(root, filename)
                self._zip.write(src_path, dst_path)

    def close(self):
        self._zip.close()


def git_clone_commit(git_url, git_commit, dst_dir):

    def git_cmd(args):
        subprocess.check_call(['git', '-C', dst_dir] + args)

    git_cmd(['init'])
    git_cmd(['remote', 'add', 'origin', git_url])
    git_cmd(['fetch', 'origin', git_commit])
    git_cmd(['reset', '--hard', git_commit])


def get_bazel_download_url(version: str) -> str:
    """Return Bazel download URL for a specific version and the current host platform."""
    if sys.platform == 'linux':
        host_os = 'linux'
    elif sys.platform == 'darwin':
        host_os = 'darwin'
    elif sys.platform in ('win32', 'cygwin'):
        host_os = 'windows'
    else:
        host_os = os.uname().sysname

    host_cpu = os.uname().machine
    if host_cpu.startswith(('armv8', 'aarch64')):
        host_cpu = 'arm64'

    ext = '.exe' if host_os == 'windows' else ''

    return f'https://github.com/bazelbuild/bazel/releases/download/{version}/bazel-{version}-{host_os}-{host_cpu}{ext}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    bazel_group = parser.add_mutually_exclusive_group(required=True)
    bazel_group.add_argument(
        "--bazel-version", help="Bazel version to download.")
    bazel_group.add_argument("--bazel", help="Path to local Bazel binary.")

    parser.add_argument(
        "--force",
        action="store_true",
        default=False,
        help="Force installation if install_dir already exists.",
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--install-dir", metavar="INSTALL_DIR", help="Install directory.")
    group.add_argument(
        "--output-archive",
        metavar="OUTPUT_ARCHIVE",
        help="Generate zip archive.")

    args = parser.parse_args()

    bazel_download_dir = None

    if args.bazel_version:
        bazel_download_dir = tempfile.TemporaryDirectory()
        bazel_bin = os.path.join(
            bazel_download_dir.name, 'bazel-' + args.bazel_version)
        url = get_bazel_download_url(args.bazel_version)
        print('Downloading %s' % url)
        urllib.request.urlretrieve(url, bazel_bin)
        os.chmod(bazel_bin, 0o750)
        args.bazel = bazel_bin

    if not os.path.exists(args.bazel):
        return parser.error("File does not exist: %s" % args.bazel)
    if not os.path.isfile(args.bazel):
        return parser.error("Not a regular file: %s" % args.bazel)

    if args.install_dir:
        if os.path.exists(args.install_dir) and not args.force:
            return parser.error(
                "Install directory already exists, use --force.")
    elif args.output_archive:
        if args.output_archive[-4:] != ".zip":
            return parser.error("Output archive path must end in '.zip'.")
    else:
        # Should not happen.
        assert False, "Invalid command-line arguments!"

    # In many installation, 'bazel' is a wrapper script, not a real
    # binary.
    with open(args.bazel, "rb") as f:
        header = f.read(2)
        if header == b"#!":
            return parser.error(
                "This is a wrapper script, not a real executable: %s" %
                args.bazel)

    # Create temporary directory to perform extraction.
    with tempfile.TemporaryDirectory() as tmpdirname:
        # Download infra-3pp scripts.
        infra_3pp_dir = os.path.join(tmpdirname, 'fuchsia-infra-3pp')
        os.makedirs(infra_3pp_dir)
        git_clone_commit(
            _INFRA_3PP_GIT_URL, _INFRA_3PP_GIT_COMMIT, infra_3pp_dir)

        # Extract everything under a temporary 'install' directory.
        temp_install_dir = os.path.join(tmpdirname, "install")
        os.makedirs(temp_install_dir)

        output_user_root = os.path.join(tmpdirname, "output_user_root")

        # Extract Bazel version from binary.
        version_out = subprocess.check_output(
            [args.bazel, '--output_user_root', output_user_root, 'version'],
            text=True)

        bazel_version = None
        version_prefix = 'Build label: '
        for line in version_out.splitlines():
            if line.startswith(version_prefix):
                bazel_version = line[len(version_prefix):]

        if not bazel_version:
            print(
                'ERROR: Could not find version of: %s' % args.bazel,
                file=sys.stderr)
            print(
                'The `version` command returned:\n%s\n' % version_out,
                file=sys.stderr)
            return 1

        print('Found Bazel version: [%s]' % bazel_version)

        # Run the install script.
        env = os.environ.copy()
        env['_3PP_VERSION'] = bazel_version
        env['_BAZEL_BIN'] = os.path.abspath(args.bazel)
        subprocess.check_call(
            [
                os.path.join(infra_3pp_dir, 'bazel', 'install.sh'),
                temp_install_dir,
            ],
            env=env)

        # Copy to the install dir or the output archive
        if args.install_dir:
            print(
                "\nCopying to install directory: %s" % args.install_dir,
                end="",
                flush=True,
            )
            out = OutputTree(args.install_dir, args.force)
        else:
            print(
                "\nCopying to output archive: %s" % args.output_archive,
                end="",
                flush=True,
            )
            out = OutputArchive(args.output_archive)

        out.add_file(args.bazel, "bazel-real")
        out.add_file(os.path.join(temp_install_dir, "bazel"), "bazel")
        out.add_file(
            os.path.join(temp_install_dir, "README.fuchsia"), "README.fuchsia")
        out.add_file(os.path.join(temp_install_dir, "LICENSE"), "LICENSE")
        out.add_tree(
            os.path.join(temp_install_dir, 'install_base'), "install_base")
        out.close()
        print("\nDone.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
