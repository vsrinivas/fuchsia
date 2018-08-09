#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys


FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # rust
    os.path.abspath(__file__))))

BUILD_CONFIG = '''
[llvm]
optimize = true
static-libstdcpp = true
ninja = true
targets = "X86;AArch64"

[build]
target = ["{target}-fuchsia"]
docs = false
extended = true
openssl-static = true

[install]
prefix = "{prefix}"
sysconfdir = "etc"

[rust]
optimize = true

[target.{target}-fuchsia]
cc = "{cc}"
cxx = "{cxx}"
ar = "{ar}"
linker = "{cc}"

[dist]
'''

CARGO_CONFIG = '''
[target.{target}-fuchsia]
linker = "{linker}"
ar = "{ar}"
rustflags = [
    "-C", "link-arg=--target={target}-fuchsia",
    "-C", "link-arg=--sysroot={sysroot}",
    "-C", "link-arg=-L{shared_libs_root}",
]
'''

def ensure_dir(dir):
    if not os.path.exists(dir):
        os.makedirs(dir)
    return dir

def main():
    parser = argparse.ArgumentParser(description='Build a Rust toolchain for Fuchsia')
    parser.add_argument(
            '--rust-root',
            help='root directory of Rust checkout',
            required=True)
    parser.add_argument(
            '--sysroot',
            help='zircon sysroot (possibly //out/release-x64/sdks/zircon_sysroot/sysroot)',
            required=True)
    parser.add_argument(
            '--shared-libs-root',
            help='shared libs root (possibly //out/release-x64/x64-shared)',
            required=True)
    parser.add_argument(
            '--host-os',
            help='host operating system',
            choices=['linux', 'mac'],
            default='linux')
    parser.add_argument(
            '--target',
            help='target architecture',
            choices=['x86_64', 'aarch64'],
            default='x86_64')
    parser.add_argument(
            '--staging-dir',
            help='directory in which to stage Rust build configuration artifacts',
            default='/tmp/fuchsia_rustc_staging')
    parser.add_argument(
            '--debug',
            help='turn on debug mode, with extra logs',
            action='store_true')
    args = parser.parse_args()

    rust_root = os.path.abspath(args.rust_root)
    sysroot = os.path.abspath(args.sysroot)
    shared_libs_root = os.path.abspath(args.shared_libs_root)
    host_os = args.host_os
    target = args.target
    staging_dir = os.path.abspath(args.staging_dir)
    debug = args.debug

    build_dir = ensure_dir(os.path.join(staging_dir, 'build'))
    toolchain_dir = ensure_dir(os.path.join(staging_dir, 'toolchain'))
    clang_dir = os.path.join(FUCHSIA_ROOT, 'buildtools', host_os + '-x64',
                             'clang')

    config_file = os.path.join(build_dir, 'config.toml')
    with open(config_file, 'w') as file:
        file.write(BUILD_CONFIG.format(
            target=target,
            prefix=toolchain_dir,
            cc=os.path.join(clang_dir, 'bin', 'clang'),
            cxx=os.path.join(clang_dir, 'bin', 'clang++'),
            ar=os.path.join(clang_dir, 'bin', 'llvm-ar'),
        ))

    cargo_dir = ensure_dir(os.path.join(staging_dir, '.cargo'))
    with open(os.path.join(cargo_dir, 'config'), 'w') as file:
        file.write(CARGO_CONFIG.format(
            target=target,
            linker=os.path.join(clang_dir, 'bin', 'clang'),
            ar=os.path.join(clang_dir, 'bin', 'llvm-ar'),
            sysroot=sysroot,
            shared_libs_root=shared_libs_root,
        ))

    cflags_key = 'CFLAGS_%s-fuchsia' % target
    cflags_val = '--target=%s-fuchsia --sysroot=%s' % (target, sysroot)

    env = {
        'CARGO_HOME': cargo_dir,
        cflags_key: cflags_val,
        'PATH': os.environ['PATH'],
        'RUST_BACKTRACE': '1',
    }

    def run_build_command(command):
        command_args = [
            os.path.join(rust_root, 'x.py'),
        ]
        command_args += command
        command_args += [
            '--config',
            config_file,
            '--src',
            rust_root,
        ]
        if debug:
            command_args.append('--verbose')
        print('Running: %s' % ' '.join(command_args))
        # The builds need to run from a subdirectory of the staging dir
        # otherwise the cargo config set up above will get clobbered by x.py.
        out_dir = ensure_dir(os.path.join(staging_dir, 'out'))
        subprocess.check_call(command_args, env=env, cwd=out_dir)

    run_build_command(['install'])

    print('The toolchain is ready at: %s' % toolchain_dir)


if __name__ == '__main__':
    main()
