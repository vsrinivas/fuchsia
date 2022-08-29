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

_SCRIPT_DIR = os.path.dirname(__file__)


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


def get_bazel_download_url(version):
    return 'https://github.com/bazelbuild/bazel/releases/download/{version}/bazel-{version}-{platform}'.format(
        version=version, platform='linux-x86_64')


def get_default_bazel_version():
    return '6.0.0-pre.20220816.1'


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
        '--bazel-version',
        default=get_default_bazel_version(),
        help='Set Bazel binary version to use.')
    parser.add_argument(
        '--quiet', action='store_true', help='Disable verbose output.')

    args = parser.parse_args()

    def log(message):
        if not args.quiet:
            print(message)

    if not args.fuchsia_dir:
        # Assume this script is under build/bazel/scripts/
        args.fuchsia_dir = os.path.abspath(
            os.path.join(_SCRIPT_DIR, '..', '..', '..'))
        log('Found Fuchsia dir: %s' % args.fuchsia_dir)

    if args.bazel_version:
        with tempfile.TemporaryDirectory() as tmpdirname:
            bazel_bin = os.path.join(tmpdirname, 'bazel-' + args.bazel_version)
            url = get_bazel_download_url(args.bazel_version)
            log('Downloading %s' % url)
            urllib.request.urlretrieve(url, bazel_bin)
            os.chmod(bazel_bin, 0o750)

            install_path = os.path.join(
                args.fuchsia_dir, 'prebuilt', 'third_party', 'bazel',
                'linux-x64')
            log('Generating Bazel install at: %s' % install_path)
            with InstallDirectory(install_path) as install:
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

    EXTERNAL_REPOSITORIES = {
        'bazel_skylib':
            {
                'git_url': 'https://github.com/bazelbuild/bazel-skylib.git',
                'git_branch': '1.2.1',
            },
        'rules_cc':
            {
                'git_url': 'https://github.com/bazelbuild/rules_cc.git',
                'git_branch': '0.0.2',
            },
        'rules_rust':
            {
                'git_url': 'https://github.com/bazelbuild/rules_rust.git',
                'git_branch': '0.9.0',
            },
        'sdk-integration':
            {
                'git_url':
                    'https://fuchsia.googlesource.com/sdk-integration.git',
                'git_commit':
                    'a32990603515455aae7c452d13975a460589a2ac',
            }
    }

    EXTERNAL_REPOSITORIES_ROOT = os.path.join(
        args.fuchsia_dir, 'third_party', 'bazel', 'repositories')

    def get_external_repository(name, info):
        out_dir = os.path.join(EXTERNAL_REPOSITORIES_ROOT, name)
        if os.path.exists(out_dir):
            shutil.rmtree(out_dir)

        log('Downloading external repository: %s' % out_dir)

        os.makedirs(out_dir)
        url = info['git_url']
        if 'git_branch' in info:
            version = info['git_branch']
            clone_git_branch(url, version, out_dir)
        elif 'git_commit' in info:
            version = info['git_commit']
            clone_git_commit(url, version, out_dir)
            version = 'git:' + version
        else:
            print(
                'ERROR: Invalid info for repository %s: %s' % (name, info),
                file=sys.stderr)
            return 1

        module_file = os.path.join(out_dir, 'MODULE.bazel')
        if os.path.exists(module_file):
            shutil.copy(module_file, module_file + '.original')

        write_file(
            module_file, f'module(name = "{name}", version = "{version}")\n')

    for name, info in EXTERNAL_REPOSITORIES.items():
        get_external_repository(name, info)

    # NOTE: Currently, a know fixed version of sdk-integration is being used
    # (determined by the git_commit value above) instead of using whatever
    # is rolled under //third_party/sdk-integration/ to minimize surprises.
    #
    # The situation will likely change in the near future when we are confident
    # enough to use the rolled version directly.
    #
    # In the meantime, add missing MODULE.bazel files to two repository
    # directories, as they are needed when enabling BzlMod for the platform
    # build. Recent versions of sdk-integration already have these files
    # so create them on deman.

    # Special case for sdk-integration/bazel_rules_fuchsia[_experimental]
    # Add a missing MODULE.bazel file to each of these directories.
    for name in ['bazel_rules_fuchsia', 'bazel_rules_fuchsia_experimental']:
        repo_name = name[len('bazel_'):]
        repo_dir = os.path.join(
            EXTERNAL_REPOSITORIES_ROOT, 'sdk-integration', name)
        module_file = os.path.join(repo_dir, 'MODULE.bazel')
        if not os.path.exists(module_file):
            log("Generating external repository: %s" % repo_dir)
            write_file(
                module_file, f'module(name = "{repo_name}", version = "1")\n')

    return 0


if __name__ == "__main__":
    sys.exit(main())
