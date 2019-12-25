#!/usr/bin/env python2.7
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build script for a Go app.

import argparse
import os
import subprocess
import sys
import string
import shutil
import errno

from gen_library_metadata import get_sources


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--godepfile', help='Path to godepfile tool', required=True)
    parser.add_argument(
        '--root-out-dir', help='Path to root of build output', required=True)
    parser.add_argument(
        '--cc', help='The C compiler to use', required=False, default='cc')
    parser.add_argument(
        '--cxx', help='The C++ compiler to use', required=False, default='c++')
    parser.add_argument(
        '--objcopy',
        help='The objcopy tool to use',
        required=False,
        default='objcopy')
    parser.add_argument('--sysroot', help='The sysroot to use', required=False)
    parser.add_argument(
        '--target', help='The compiler target to use', required=False)
    parser.add_argument(
        '--depfile', help='The path to the depfile', required=True)
    parser.add_argument(
        '--current-cpu',
        help='Target architecture.',
        choices=['x64', 'arm64'],
        required=True)
    parser.add_argument(
        '--current-os',
        help='Target operating system.',
        choices=['fuchsia', 'linux', 'mac', 'win'],
        required=True)
    parser.add_argument(
        '--buildidtool', help='The path to the buildidtool.', required=True)
    parser.add_argument(
        '--build-id-dir',
        help='The path to the .build-id directory.',
        required=True)
    parser.add_argument(
        '--go-root', help='The go root to use for builds.', required=True)
    parser.add_argument(
        '--go-cache', help='Cache directory to use for builds.', required=False)
    parser.add_argument(
        '--is-test', help='True if the target is a go test', default=False)
    parser.add_argument('--buildmode', help='Build mode to use')
    parser.add_argument('--gcflags', help='Arguments to pass to Go compiler')
    parser.add_argument(
        '--go-dep-files',
        help='List of files describing library dependencies',
        nargs='*',
        default=[])
    parser.add_argument('--binname', help='Output file', required=True)
    parser.add_argument(
        '--output-path',
        help='Where to output the (unstripped) binary',
        required=True)
    parser.add_argument(
        '--stripped-output-path',
        help='Where to output a stripped binary, if supplied')
    parser.add_argument(
        '--verbose',
        help='Tell the go tool to be verbose about what it is doing',
        action='store_true')
    parser.add_argument('--package', help='The package name', required=True)
    parser.add_argument(
        '--include-dir',
        help='-isystem path to add',
        action='append',
        default=[])
    parser.add_argument(
        '--lib-dir', help='-L path to add', action='append', default=[])
    parser.add_argument('--vet', help='Run go vet', action='store_true')
    parser.add_argument(
        '--tag', help='Add a go build tag', default=[], action='append')
    args = parser.parse_args()

    try:
        os.makedirs(args.go_cache)
    except OSError as e:
        if e.errno == errno.EEXIST and os.path.isdir(args.go_cache):
            pass
        else:
            raise

    goarch = {
        'x64': 'amd64',
        'arm64': 'arm64',
    }[args.current_cpu]
    goos = {
        'fuchsia': 'fuchsia',
        'linux': 'linux',
        'mac': 'darwin',
        'win': 'windows',
    }[args.current_os]

    build_id_dir = os.path.join(args.root_out_dir, '.build-id')
    dist = args.stripped_output_path or args.output_path

    # Project path is a package specific gopath, also known as a "project" in go parlance.
    project_path = os.path.join(
        args.root_out_dir, 'gen', 'gopaths', args.binname)

    # Clean up any old project path to avoid leaking old dependencies
    shutil.rmtree(os.path.join(project_path, 'src'), ignore_errors=True)
    os.makedirs(os.path.join(project_path, 'src'))

    if args.go_dep_files:
        # Create a gopath for the packages dependency tree
        for dst, src in get_sources(args.go_dep_files).items():
            dstdir = os.path.join(project_path, 'src', os.path.dirname(dst))
            try:
                os.makedirs(dstdir)
            except OSError as e:
                # EEXIST occurs if two gopath entries share the same parent name
                if e.errno != errno.EEXIST:
                    raise
            # TODO(BLD-228): the following check might not be necessary anymore.
            tgt = os.path.join(dstdir, os.path.basename(dst))
            # The source tree is effectively read-only once the build begins.
            # Therefore it is an error if tgt is in the source tree. At first
            # glance this may seem impossible, but it can happen if dst is foo/bar
            # and foo is a symlink back to the source tree.
            canon_root_out_dir = os.path.realpath(args.root_out_dir)
            canon_tgt = os.path.realpath(tgt)
            if not canon_tgt.startswith(canon_root_out_dir):
                raise ValueError(
                    "Dependency destination not in --root-out-dir: provided=%s, path=%s, realpath=%s"
                    % (dst, tgt, canon_tgt))
            os.symlink(os.path.relpath(src, os.path.dirname(tgt)), tgt)

    cflags = []
    if args.sysroot:
        cflags.append('--sysroot=' + args.sysroot)
    if args.target:
        cflags.append('--target=' + args.target)
    ldflags = cflags[:]

    cflags += ['-isystem' + dir for dir in args.include_dir]
    ldflags += ['-L' + dir for dir in args.lib_dir]

    cflags_joined = ' '.join(cflags)
    ldflags_joined = ' '.join(ldflags)

    gopath = os.path.abspath(project_path)
    build_goroot = os.path.abspath(args.go_root)

    env = {
        # /usr/bin:/bin are required for basic things like bash(1) and env(1). Note
        # that on Mac, ld is also found from /usr/bin.
        'PATH': "/usr/bin:/bin",
        'GOARCH': goarch,
        'GOOS': goos,
        'GOPATH': gopath,
        # Some users have GOROOT set in their parent environment, which can break
        # things, so it is always set explicitly here.
        'GOROOT': build_goroot,
        'GOCACHE': args.go_cache,
        'CC': args.cc,
        'CXX': args.cxx,
        'CGO_CFLAGS': cflags_joined,
        'CGO_CPPFLAGS': cflags_joined,
        'CGO_CXXFLAGS': cflags_joined,
        'CGO_LDFLAGS': ldflags_joined,
        'CGO_ENABLED': '1',
    }

    if args.target:
        env['CC_FOR_TARGET'] = env['CC']
        env['CXX_FOR_TARGET'] = env['CXX']

    go_tool = os.path.join(build_goroot, 'bin', 'go')

    if args.vet:
        retcode = subprocess.call([go_tool, 'vet', args.package], env=env)
        if retcode != 0:
            return retcode

    cmd = [go_tool]
    if args.is_test:
        cmd += ['test', '-c']
    else:
        cmd += ['build']
    if args.verbose:
        cmd += ['-x']
    if args.tag:
        # Separate tags by spaces. This behavior is actually deprecated in the
        # go command line, but Fuchsia currently has an older version of go
        # that hasn't switched to commas.
        cmd += ['-tags', ' '.join(args.tag)]
    if args.buildmode:
        cmd += ['-buildmode', args.buildmode]
    if args.gcflags:
        cmd += ['-gcflags', args.gcflags]
    cmd += [
        '-pkgdir',
        os.path.join(project_path, 'pkg'), '-o', args.output_path, args.package
    ]
    retcode = subprocess.call(cmd, env=env)

    if retcode == 0 and args.stripped_output_path:
        if args.current_os == 'mac':
            retcode = subprocess.call(
                [
                    'xcrun', 'strip', '-x', args.output_path, '-o',
                    args.stripped_output_path
                ],
                env=env)
        else:
            retcode = subprocess.call(
                [
                    args.objcopy, '--strip-sections', args.output_path,
                    args.stripped_output_path
                ],
                env=env)

    # TODO(fxbug.dev/27215): Also invoke the buildidtool in the case of linux
    # once buildidtool knows how to deal in Go's native build ID format.
    supports_build_id = args.current_os == 'fuchsia'
    if retcode == 0 and supports_build_id:
        retcode = subprocess.call(
            [
                args.buildidtool, "-build-id-dir", args.build_id_dir, "-stamp",
                dist + ".build-id.stamp", "-entry",
                ".debug=" + args.output_path, "-entry", "=" + dist,
            ])

    if retcode == 0:
        if args.depfile is not None:
            with open(args.depfile, "wb") as out:
                godepfile_args = [args.godepfile, '-o', dist]
                if args.is_test:
                    godepfile_args += ['-test']
                godepfile_args += [args.package]
                subprocess.Popen(godepfile_args, stdout=out, env=env)

    return retcode


if __name__ == '__main__':
    sys.exit(main())
