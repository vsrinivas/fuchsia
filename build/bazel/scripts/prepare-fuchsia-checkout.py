#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Prepare a Fuchsia checkout for 'fx bazel'. This is a temporary measure
until all requirements are properly managed with 'jiri'."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

_SCRIPT_DIR = os.path.dirname(__file__)


def get_host_platform() -> str:
    '''Return host platform name, following Fuchsia conventions.'''
    if sys.platform == 'linux':
        return 'linux'
    elif sys.platform == 'darwin':
        return 'mac'
    else:
        return os.uname().sysname


def get_host_arch() -> str:
    '''Return host CPU architecture, following Fuchsia conventions.'''
    host_arch = os.uname().machine
    if host_arch == 'x86_64':
        return 'x64'
    elif host_arch.startswith(('armv8', 'aarch64')):
        return 'arm64'
    else:
        return host_arch


def get_host_tag():
    '''Return host tag, following Fuchsia conventions.'''
    return '%s-%s' % (get_host_platform(), get_host_arch())


def write_file(path, content):
    with open(path, 'w') as f:
        f.write(content)


def clone_git_branch(git_url, git_branch, dst_dir):
    subprocess.check_call(
        ['git', 'clone', '--branch', git_branch, '--depth=1', git_url, dst_dir],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)


def clone_git_commit(git_url, git_commit, dst_dir):

    def git_cmd(args):
        subprocess.check_call(
            ['git', '-C', dst_dir] + args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)

    git_cmd(['init'])
    git_cmd(['remote', 'add', 'origin', git_url])
    git_cmd(['fetch', 'origin', git_commit])
    git_cmd(['reset', '--hard', 'FETCH_HEAD'])


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


def get_bazel_version(bazel_launcher):
    """Return version of a given Bazel binary."""
    output = subprocess.check_output(
        [bazel_launcher, "version"], stderr=subprocess.DEVNULL, text=True)
    version_prefix = 'Build label: '
    for line in output.splitlines():
        if line.startswith(version_prefix):
            return line[len(version_prefix):].strip()
    return None


def ignore_log(message):
    pass


_FALLBACK_BAZEL_VERSION = '5.3.0'


def get_jiri_bazel_version(fuchsia_dir, log=ignore_log):
    # The location of the file that contains the Jiri Bazel package definition
    prebuilts_file = os.path.join(
        fuchsia_dir, 'integration', 'fuchsia', 'prebuilts')
    if not os.path.exists(prebuilts_file):
        log(
            'Could not find Jiri file defining Bazel version at: %s (using fallback version %s)'
            % (prebuilts_file, _FALLBACK_BAZEL_VERSION))
        return _FALLBACK_BAZEL_VERSION

    version = None
    prebuilts = ET.parse(prebuilts_file)
    for package in prebuilts.findall('packages/package'):
        package_name = package.get('name')
        if package_name == 'fuchsia/third_party/bazel/${platform}':
            version = package.get('version')
            break

    if not version:
        log(
            'Could not find Bazel package in: %s (using fallback version %s)' %
            (prebuilts_file, _FALLBACK_BAZEL_VERSION))
        return _FALLBACK_BAZEL_VERSION

    # The package version has a .<patch> sufix which corresponds to
    # the LUCI recipe patch number, so remove it.
    pos = version.rfind('.')
    if pos > 0:
        version = version[:pos]

    version_prefix = 'version:2@'
    if not version.startswith(version_prefix):
        log(
            'Unsupported Bazel version tag (%s), using fallback %s' %
            (version, _FALLBACK_BAZEL_VERSION))
        return _FALLBACK_BAZEL_VERSION

    return version[len(version_prefix):]


class InstallDirectory(object):

    def __init__(self, path):
        self._tmp_dir = path + ".tmp"
        self._dst_dir = path
        self._old_dir = path + ".old"

    @property
    def path(self):
        return self._tmp_dir

    @property
    def final_path(self):
        return self._dst_dir

    def __enter__(self):
        if os.path.exists(self._tmp_dir):
            shutil.rmtree(self._tmp_dir)
        os.makedirs(self._tmp_dir)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is not None:
            # An exception occurred, cleanup temp dir.
            shutil.rmtree(self._tmp_dir)
        else:
            if os.path.exists(self._old_dir):
                shutil.rmtree(self._old_dir)
            os.rename(self._dst_dir, self._old_dir)
            os.rename(self._tmp_dir, self._dst_dir)
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--fuchsia-dir', help='Path to Fuchsia checkout directory.')
    parser.add_argument(
        '--bazel-version', help='Set Bazel binary version to use.')
    parser.add_argument(
        '--quiet', action='store_true', help='Disable verbose output.')

    args = parser.parse_args()

    def log(message):
        if not args.quiet:
            print(message, flush=True)

    if not args.fuchsia_dir:
        # Assume this script is under build/bazel/scripts/
        args.fuchsia_dir = os.path.abspath(
            os.path.join(_SCRIPT_DIR, '..', '..', '..'))
        log('Found Fuchsia dir: %s' % args.fuchsia_dir)

    if not args.bazel_version:
        args.bazel_version = get_jiri_bazel_version(args.fuchsia_dir, log=log)
        if not args.bazel_version:
            return 1
        log('Using default Bazel version: ' + args.bazel_version)

    # Compare the available Bazel version with the one we need.
    bazel_install_path = os.path.join(
        args.fuchsia_dir, 'prebuilt', 'third_party', 'bazel', get_host_tag())
    bazel_launcher = os.path.join(bazel_install_path, 'bazel')
    bazel_update = not os.path.exists(bazel_launcher)
    if not bazel_update:
        current_version = get_bazel_version(bazel_launcher)
        if current_version != args.bazel_version:
            log(
                'Found installed Bazel version %s, updating to %s' %
                (current_version, args.bazel_version))
            bazel_update = True

    if bazel_update:
        with tempfile.TemporaryDirectory() as tmpdirname:
            bazel_bin = os.path.join(tmpdirname, 'bazel-' + args.bazel_version)
            url = get_bazel_download_url(args.bazel_version)
            log('Downloading %s' % url)
            urllib.request.urlretrieve(url, bazel_bin)
            os.chmod(bazel_bin, 0o750)

            log('Generating Bazel install at: %s' % bazel_install_path)
            with InstallDirectory(bazel_install_path) as install:
                subprocess.check_call(
                    [
                        sys.executable,
                        '-S',
                        os.path.join(_SCRIPT_DIR, 'generate-bazel-install.py'),
                        '--bazel',
                        bazel_bin,
                        '--install-dir',
                        install.path,
                        '--force',
                    ],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE)

    return 0


if __name__ == "__main__":
    sys.exit(main())
