#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Update or generate Bazel workspace for the platform build.

The script first checks whether the Ninja build plan needs to be updated.
After that, it checks whether the Bazel workspace used by the platform
build, and associatedd files (e.g. bazel launcher script) and
directories (e.g. output_base) need to be updated. It simply exits
if no update is needed, otherwise, it will regenerate everything
appropriately.

The TOPDIR directory argument will be populated with the following
files:

  $TOPDIR/
    bazel                   Bazel launcher script.
    generated-info.json     State of inputs during last generation.
    output_base/            Bazel output base.
    output_user_root/       Baze output user root.
    workspace/              Bazel workspace directory.

The workspace/ sub-directory will be populated with symlinks
mirroring the top FUCHSIA_DIR directory, except for the 'out'
sub-directory and a few other files. It will also include a few
generated files (e.g. `.bazelrc`).

The script tracks the file and sub-directory entries of $FUCHSIA_DIR,
and is capable of updating the workspace if new ones are added, or
old ones are removed.
"""

import argparse
import difflib
import errno
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys


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


def get_host_tag() -> str:
    '''Return host tag, following Fuchsia conventions.'''
    return '%s-%s' % (get_host_platform(), get_host_arch())


def force_symlink(target_path: str, dst_path: str):
    '''Create a symlink at |dst_path| that points to |target_path|.'''
    dst_dir = os.path.dirname(dst_path)
    os.makedirs(dst_dir, exist_ok=True)
    target_path = os.path.relpath(target_path, dst_dir)
    try:
        os.symlink(target_path, dst_path)
    except OSError as e:
        if e.errno == errno.EEXIST:
            os.remove(dst_path)
            os.symlink(target_path, dst_path)
        else:
            raise


def make_removeable(path: str):
    '''Ensure the file at |path| is removeable.'''
    info = os.stat(path, follow_symlinks=False)
    if info.st_mode & stat.S_IWUSR == 0:
        os.chmod(path, info.st_mode | stat.S_IWUSR, follow_symlinks=False)


def remove_dir(path):
    '''Properly remove a directory.'''
    # shutil.rmtree() does not work well when there are readonly symlinks to
    # directories. This results in weird NotADirectory error when trying to
    # call os.scandir() or os.rmdir() on them (which happens internally).
    #
    # Re-implement it correctly here. This is not as secure as it could
    # (see "shutil.rmtree symlink attack"), but is sufficient for the Fuchsia
    # build.
    all_files = []
    all_dirs = []
    for root, subdirs, files in os.walk(path):
        # subdirs may contain actual symlinks which should be treated as
        # files here.
        real_subdirs = []
        for subdir in subdirs:
            if os.path.islink(os.path.join(root, subdir)):
                files.append(subdir)
            else:
                real_subdirs.append(subdir)

        for file in files:
            file_path = os.path.join(root, file)
            all_files.append(file_path)
            make_removeable(file_path)
        for subdir in real_subdirs:
            dir_path = os.path.join(root, subdir)
            all_dirs.append(dir_path)
            make_removeable(dir_path)

    for file in reversed(all_files):
        os.remove(file)
    for dir in reversed(all_dirs):
        os.rmdir(dir)
    os.rmdir(path)


def create_clean_dir(path):
    '''Create a clean directory.'''
    if os.path.exists(path):
        remove_dir(path)
    os.makedirs(path)


def get_fx_build_dir(fuchsia_dir):
    """Return the path to the Ninja build directory set through 'fx set'.

    Args:
      fuchsia_dir: The Fuchsia top-level source directory.
    Returns:
      The path to the Ninja build directory, or None if it could not be
      determined, which happens on infra bots which do not use 'fx set'
      but 'fint set' directly.
    """
    fx_build_dir_path = os.path.join(fuchsia_dir, '.fx-build-dir')
    if not os.path.exists(fx_build_dir_path):
        return None

    with open(fx_build_dir_path) as f:
        build_dir = f.read().strip()
        return os.path.join(fuchsia_dir, build_dir)


class GeneratedFiles(object):
    """Models the content of a generated Bazel workspace."""

    def __init__(self, files={}):
        self._files = files

    def _check_new_path(self, path):
        assert path not in self._files, (
            'File entry already in generated list: ' + path)

    def add_symlink(self, dst_path, target_path):
        self._check_new_path(dst_path)
        self._files[dst_path] = {
            "type": "symlink",
            "target": target_path,
        }

    def add_file(self, dst_path, content, executable=False):
        self._check_new_path(dst_path)
        entry = {
            "type": "file",
            "content": content,
        }
        if executable:
            entry["executable"] = True
        self._files[dst_path] = entry

    def add_file_hash(self, dst_path):
        self._check_new_path(dst_path)
        h = hashlib.new('md5')
        with open(dst_path, 'rb') as f:
            h.update(f.read())
        self._files[dst_path] = {
            "type": "md5",
            "hash": h.hexdigest(),
        }

    def add_top_entries(self, fuchsia_dir, subdir, excluded_file):
        for name in os.listdir(fuchsia_dir):
            if not excluded_file(name):
                self.add_symlink(
                    os.path.join(subdir, name), os.path.join(fuchsia_dir, name))

    def to_json(self):
        """Convert to JSON file."""
        return json.dumps(self._files, indent=2, sort_keys=True)

    def write(self, out_dir):
        """Write workspace content to directory."""
        for path, entry in self._files.items():
            type = entry["type"]
            if type == "symlink":
                target_path = entry["target"]
                link_path = os.path.join(out_dir, path)
                force_symlink(target_path, link_path)
            elif type == "file":
                file_path = os.path.join(out_dir, path)
                with open(file_path, 'w') as f:
                    f.write(entry["content"])
                if entry.get("executable", False):
                    os.chmod(file_path, 0o755)
            elif type == 'md5':
                # Nothing to do here.
                pass
            else:
                assert False, 'Unknown entry type: ' % entry["type"]


def ninja_regen_check(gn_output_dir, ninja):
    '''Return true if the Ninja build plan needs to be regenerated.'''
    # This reads the build.ninja.d directly and tries to stat() all
    # dependencies in it directly (around 7000+), which is much
    # faster than Ninja trying to stat all build graph paths!
    build_ninja_d = os.path.join(gn_output_dir, 'build.ninja.d')
    if not os.path.exists(build_ninja_d):
        return False

    with open(build_ninja_d) as f:
        build_ninja_deps = f.read().split(' ')

    assert len(build_ninja_deps) > 1
    ninja_stamp = os.path.join(gn_output_dir, build_ninja_deps[0][:-1])
    ninja_stamp_timestamp = os.stat(ninja_stamp).st_mtime

    for dep in build_ninja_deps[1:]:
        dep_path = os.path.join(gn_output_dir, dep)
        if not os.path.exists(dep_path):
            return True

        dep_timestamp = os.stat(dep_path).st_mtime
        if dep_timestamp > ninja_stamp_timestamp:
            return True

    return False


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument(
        '--fuchsia-dir',
        help='Path to the Fuchsia source tree, auto-detected by default.')
    parser.add_argument(
        '--gn_output_dir',
        help='GN output directory, auto-detected by default.')
    parser.add_argument(
        '--bazel-bin',
        help=
        'Path to bazel binary, defaults to $FUCHSIA_DIR/prebuilt/third_party/bazel/${host_platform}/bazel'
    )
    parser.add_argument(
        '--topdir',
        help='Top output directory. Defaults to GN_OUTPUT_DIR/gen/build/bazel')
    parser.add_argument(
        '--use-bzlmod',
        action='store_true',
        help='Use BzlMod to generate external repositories.')
    parser.add_argument(
        '--verbose', action='count', default=1, help='Increase verbosity')
    parser.add_argument(
        '--quiet', action='count', default=0, help='Reduce verbosity')
    parser.add_argument(
        '--force',
        action='store_true',
        help='Force workspace regeneration, by default this only happens ' +
        'the script determines there is a need for it.')
    args = parser.parse_args()

    verbosity = args.verbose - args.quiet

    if not args.fuchsia_dir:
        # Assume this script is in 'build/bazel/scripts/'
        # //build/bazel:generate_main_workspace always sets this argument,
        # this feature is a convenience when calling this script manually
        # during platform build development and debugging.
        args.fuchsia_dir = os.path.join(
            os.path.dirname(__file__), '..', '..', '..')

    fuchsia_dir = os.path.abspath(args.fuchsia_dir)

    if not args.gn_output_dir:
        args.gn_output_dir = get_fx_build_dir(fuchsia_dir)
        if not args.gn_output_dir:
            parser.error(
                'Could not auto-detect build directory, please use --gn_output_dir=DIR'
            )
            return 1

    gn_output_dir = os.path.abspath(args.gn_output_dir)

    if not args.topdir:
        args.topdir = os.path.join(gn_output_dir, 'gen/build/bazel')

    topdir = os.path.abspath(args.topdir)

    logs_dir = os.path.join(topdir, 'logs')

    host_tag = get_host_tag()

    ninja_binary = os.path.join(
        fuchsia_dir, 'prebuilt', 'third_party', 'ninja', host_tag, 'ninja')

    output_base_dir = os.path.abspath(os.path.join(topdir, 'output_base'))
    output_user_root = os.path.abspath(os.path.join(topdir, 'output_user_root'))
    workspace_dir = os.path.abspath(os.path.join(topdir, 'workspace'))

    if not args.bazel_bin:
        args.bazel_bin = os.path.join(
            fuchsia_dir, 'prebuilt', 'third_party', 'bazel', host_tag, 'bazel')

    bazel_bin = os.path.abspath(args.bazel_bin)

    bazel_launcher = os.path.abspath(os.path.join(topdir, 'bazel'))

    def log(message, level=1):
        if verbosity >= level:
            print(message)

    def log2(message):
        log(message, 2)

    log2(
        '''Using directories and files:
  Fuchsia:                {}
  GN build:               {}
  Ninja binary:           {}
  Bazel source:           {}
  Topdir:                 {}
  Logs directory:         {}
  Bazel workspace:        {}
  Bazel output_base:      {}
  Bazel output user root: {}
  Bazel launcher:         {}
'''.format(
            fuchsia_dir, gn_output_dir, ninja_binary, bazel_bin, topdir,
            logs_dir, workspace_dir, output_base_dir, output_user_root,
            bazel_launcher))

    if ninja_regen_check(gn_output_dir, ninja_binary):
        log('Re-generating Ninja build plan!')
        subprocess.run([ninja_binary, '-C', gn_output_dir, 'build.ninja'])
    else:
        log2('Ninja build plan up to date.')

    generated = GeneratedFiles()

    def write_workspace_file(path, content):
        generated.add_file('workspace/' + path, content)

    def create_workspace_symlink(path, target_path):
        generated.add_symlink('workspace/' + path, target_path)

    script_path = os.path.relpath(__file__, fuchsia_dir)

    if args.use_bzlmod:
        generated.add_file(
            'workspace/WORKSPACE.bazel',
            '# Empty on purpose, see MODULE.bazel\n')

        generated.add_symlink(
            'workspace/WORKSPACE.bzlmod',
            os.path.join(
                fuchsia_dir, 'build', 'bazel', 'toplevel.WORKSPACE.bzlmod'))

        generated.add_symlink(
            'workspace/MODULE.bazel',
            os.path.join(
                fuchsia_dir, 'build', 'bazel', 'toplevel.MODULE.bazel'))
    else:
        generated.add_symlink(
            'workspace/WORKSPACE.bazel',
            os.path.join(
                fuchsia_dir, 'build', 'bazel', 'toplevel.WORKSPACE.bazel'))

    # Generate symlinks

    def excluded_file(path):
        """Return true if a file path must be excluded from the symlink list."""
        # Never symlink to the 'out' directory.
        if path == "out":
            return True
        # Don't symlink the Jiri files, this can confuse Jiri during an 'jiri update'
        # Don't symlink the .fx directory (TODO(digit): I don't remember why?)
        # Don´t symlink the .git directory as well, since it needs to be handled separately.
        if path.startswith(('.jiri', '.fx', '.git')):
            return True
        return False

    generated.add_top_entries(fuchsia_dir, 'workspace', excluded_file)

    generated.add_symlink(
        'workspace/BUILD.bazel',
        os.path.join(fuchsia_dir, 'build', 'bazel', 'toplevel.BUILD.bazel'))

    # The top-level .git directory must be symlinked because some actions actually
    # launch git commands (e.g. to generate a build version identifier). On the other
    # hand Jiri will complain if it finds a .git repository with Jiri metadata that
    # it doesn't know about in its manifest. The error looks like:
    #
    # ```
    # [17:49:48.200] WARN: Project "fuchsia" has path /work/fx-bazel-build, but was found in /work/fx-bazel-build/out/default/gen/build/bazel/output_base/execroot/main.
    # jiri will treat it as a stale project. To remove this warning please delete this or move it out of your root folder
    # ```
    #
    # Looking at the Jiri sources reveals that it is looking at a `.git/jiri` sub-directory
    # in all git directories it finds during a `jiri update` operation. To avoid the complaint
    # then symlink all $FUCHSIA_DIR/.git/ files and directories, except the 'jiri' one.
    fuchsia_git_dir = os.path.join(fuchsia_dir, '.git')
    for git_file in os.listdir(fuchsia_git_dir):
        if git_file != 'jiri':
            generated.add_symlink(
                'workspace/.git/' + git_file,
                os.path.join(fuchsia_git_dir, git_file))

    # Generate the content of .bazelrc
    bazelrc_content = '''# Auto-generated - DO NOT EDIT!

# Ensure that platform-based C++ toolchain selection is performed, instead
# of relying on --crosstool_top / --cpu / --compiler/
build --incompatible_enable_cc_toolchain_resolution

# Setup the default platform.
# TODO(digit): Switch to //build/bazel/platforms:common
build --platforms=//build/bazel/platforms:linux_x64

# Setup remote build
build:remote --remote_instance_name=projects/rbe-fuchsia-prod/instances/default
build:remote --java_runtime_version=rbe_jdk
build:remote --tool_java_runtime_version=rbe_jdk
build:remote --action_env=BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1
build:remote --define=EXECUTOR=remote
build:remote --remote_executor=grpcs://remotebuildexecution.googleapis.com
build:remote --incompatible_strict_action_env=true
build:remote --google_default_credentials=true

# TODO(fxbug.dev/114100): use @prebuilt_clang//:host_clang_linux_x64_cc_toolchain
build:remote --extra_toolchains=//build/rbe/bazel/config:cc-toolchain
# TODO(fxbug.dev/114100): see if this can be removed or simplified
build:remote --host_platform=//build/rbe/bazel/config:platform
'''.format(ninja_output_dir=gn_output_dir)

    if args.use_bzlmod:
        bazelrc_content += '''
# Enable BlzMod, i.e. support for MODULE.bazel files.
common --experimental_enable_bzlmod
'''

    bazelrc_content += '''
# Save workspace rule events to a log file for later analysis.
build --experimental_workspace_rules_log_file={log_file}
'''.format(log_file=os.path.join(logs_dir, 'workspace-events.log'))

    generated.add_file('workspace/.bazelrc', bazelrc_content)

    # Create a symlink to the GN-generated file that will contain the list
    # of @legacy_ninja_build_outputs entries. This file is generated by the
    # GN target //build/bazel:legacy_ninja_build_outputs.
    generated.add_symlink(
        'workspace/bazel_inputs_manifest.json',
        os.path.join(
            workspace_dir, '..',
            'legacy_ninja_build_outputs.inputs_manifest.json'))

    # Generate wrapper script in topdir/bazel that invokes Bazel with the right --output_base.
    bazel_launcher_content = r'''#!/bin/bash
# Auto-generated - DO NOT EDIT!
readonly _SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" >/dev/null 2>&1 && pwd)"
readonly _WORKSPACE_DIR="${{_SCRIPT_DIR}}/{workspace}"
readonly _OUTPUT_BASE="${{_SCRIPT_DIR}}/{output_base}"
readonly _OUTPUT_USER_ROOT="${{_SCRIPT_DIR}}/{output_user_root}"
readonly _LOG_DIR="{logs_dir}"

# Exported explicitly to be used by repository rules to reference the
# Ninja output directory and binary.
export BAZEL_FUCHSIA_NINJA_OUTPUT_DIR="{ninja_output_dir}"
export BAZEL_FUCHSIA_NINJA_PREBUILT="{ninja_prebuilt}"

# Implement log rotation (up to 3 old files)
# $1: log file name (e.g. "path/to/workspace-events.log")
logrotate3 () {{
  local i
  local prev_log="$1.3"
  local cur_log
  for i in "2" "1"; do
    rm -f "${{prev_log}}"
    cur_log="$1.$i"
    if [[ -f "${{cur_log}}" ]]; then
      mv "${{cur_log}}" "${{prev_log}}"
    fi
    prev_log="${{cur_log}}"
  done
  cur_log="$1"
  if [[ -f "${{cur_log}}" ]]; then
    mv "${{cur_log}}" "${{prev_log}}"
  fi
}}

# Rotate the workspace events log. Note that this file is created
# through an option set in the .bazelrc file, not the command-line below.
mkdir -p "${{_LOG_DIR}}"
logrotate3 "${{_LOG_DIR}}/workspace-events.log"

# Setting $USER so `bazel` won't fail in environments with fake UIDs. Even if
# the USER is not actually used. See https://fxbug.dev/112206#c9.
#
# Explanation for flags:
#  --nohome_rc: Ignore $HOME/.bazelrc to enforce hermiticity / reproducibility.
#  --output_base: Ensure the output base is in the Ninja output directory, not under $HOME.
#  --output_user_root: Ensure the output user root is in the Ninja output directory, not under $HOME.
cd "${{_WORKSPACE_DIR}}" && USER=unused-bazel-build-user {bazel_bin_path} \
      --nohome_rc \
      --output_base="${{_OUTPUT_BASE}}" \
      --output_user_root="${{_OUTPUT_USER_ROOT}}" \
      "$@"
'''.format(
        ninja_output_dir=os.path.abspath(gn_output_dir),
        ninja_prebuilt=os.path.abspath(ninja_binary),
        bazel_bin_path=os.path.abspath(bazel_bin),
        logs_dir=os.path.abspath(logs_dir),
        workspace=os.path.relpath(workspace_dir, topdir),
        output_base=os.path.relpath(output_base_dir, topdir),
        output_user_root=os.path.relpath(output_user_root, topdir))

    # Ensure regeneration when this script's content changes!
    generated.add_file_hash(os.path.abspath(__file__))

    force = args.force
    generated.add_file('bazel', bazel_launcher_content, executable=True)
    generated_json = generated.to_json()
    generated_info_file = os.path.join(topdir, 'generated-info.json')
    if not os.path.exists(generated_info_file):
        log2("Missing file: " + generated_info_file)
        force = True
    elif not os.path.isdir(workspace_dir):
        log2("Missing directory: " + workspace_dir)
        force = True
    elif not os.path.isdir(output_base_dir):
        log2("Missing directory: " + output_base_dir)
        force = True
    else:
        with open(generated_info_file) as f:
            existing_info = f.read()

        if existing_info != generated_json:
            log2("Changes in %s" % (generated_info_file))
            if verbosity >= 2:
                print(
                    '\n'.join(
                        difflib.unified_diff(
                            existing_info.splitlines(),
                            generated_json.splitlines())))
            force = True

    if force:
        log(
            "Regenerating Bazel workspace%s." %
            (", --force used" if args.force else ""))
        create_clean_dir(workspace_dir)
        create_clean_dir(output_base_dir)
        generated.write(topdir)
        with open(generated_info_file, 'w') as f:
            f.write(generated_json)
    else:
        log2("Mothing to do (no changes detected)")

    # Done!
    return 0


if __name__ == "__main__":
    sys.exit(main())
