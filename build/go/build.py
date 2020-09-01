#!/usr/bin/env python3.8
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
        '--dump-syms', help='The dump_syms tool to use', required=False)
    parser.add_argument(
        '--objcopy',
        help='The objcopy tool to use',
        required=False,
        default='objcopy')
    parser.add_argument('--sysroot', help='The sysroot to use', required=False)
    parser.add_argument(
        '--target', help='The compiler target to use', required=False)
    parser.add_argument(
        '--depfile', help='The path to the depfile', required=False)
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
    parser.add_argument('--buildidtool', help='The path to the buildidtool.')
    parser.add_argument(
        '--build-id-dir', help='The path to the .build-id directory.')
    parser.add_argument(
        '--go-root', help='The go root to use for builds.', required=True)
    parser.add_argument(
        '--go-cache', help='Cache directory to use for builds.', required=False)
    parser.add_argument(
        '--is-test', help='True if the target is a go test', default=False)
    parser.add_argument('--buildmode', help='Build mode to use')
    parser.add_argument(
        '--gcflag',
        help='Arguments to pass to Go compiler',
        action='append',
        default=[])
    parser.add_argument(
        '--ldflag',
        help='Arguments to pass to Go linker',
        action='append',
        default=[])
    parser.add_argument(
        '--go-dep-files',
        help='List of files describing library dependencies',
        nargs='*',
        default=[])
    parser.add_argument(
        '--root-build-dir',
        help='Root build directory. Required if --go-dep-files is used.')
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
    parser.add_argument(
        '--cgo', help='Whether to enable CGo', action='store_true')
    parser.add_argument(
        '--source_list_path',
        help='The list of sources that were listed as deps of the go binary',
        default=[])
    parser.add_argument(
        '--verify_depfile_script',
        help='The script to use to verify source listings',
        default='')
    parser.add_argument(
        '--gn_target_name', help='The name of the gn target', default='')
    parser.add_argument(
        '--check_sources',
        help=
        'Whether or not to verify the sources in the gn targets match the generated depfile',
        action='store_true')
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

    # Clean up any old project path to avoid leaking old dependencies.
    gopath_src = os.path.join(project_path, 'src')
    if os.path.exists(gopath_src):
        shutil.rmtree(gopath_src)
    os.makedirs(gopath_src)

    link_to_source_list = []
    if args.go_dep_files:
        assert args.root_build_dir, (
            '--root-build-dir is required with --go-dep-files')

        root_build_dir = os.path.abspath(args.root_build_dir)
        link_to_source = {}

        # Create a GOPATH for the packages dependency tree.
        for dst, src in sorted(get_sources(args.go_dep_files).items()):
            # Determine if the package should be mapped recursively or only the
            # package source itself should be mapped.
            #
            # There are cases (e.g for fidl generated source) where a single
            # source is directly mapped instead of a formal Go package path.
            # This is incorrect but happened to work, so people started to
            # depend on that. In this case, we use the recurse mode, which
            # happens to do what is needed; map the file directly as a symlink.
            recurse = dst.endswith('.go')
            # - src can have a '/...' suffix like with
            #   'github.com/google/go-cmp/...'.
            # - dst have the suffix when defining a package.
            # - src can only have the suffix if dst has it too.
            assert dst.endswith('/...') >= src.endswith('/...'), (dst, src)
            if dst.endswith('/...'):
                dst = dst[:-4]
                recurse = True
                if src.endswith('/...'):
                    src = src[:-4]
            dstdir = os.path.join(gopath_src, dst)

            if recurse:
                # Map the whole directory, which implicitly makes Go subpackages
                # (subdirectories) available.
                parent = os.path.dirname(dstdir)
                if not os.path.exists(parent):
                    os.makedirs(parent)
                os.symlink(src, dstdir)
                link_to_source[os.path.join(root_build_dir, dstdir)] = src
            else:
                # Map individual files since the dependency is only on the
                # package itself, not Go subpackages. The only exception is
                # 'testdata'.
                os.makedirs(dstdir)
                for filename in os.listdir(src):
                    src_file = os.path.join(src, filename)
                    if filename == 'testdata' or os.path.isfile(src_file):
                        os.symlink(src_file, os.path.join(dstdir, filename))
                        link_to_source[os.path.join(
                            root_build_dir, dstdir, filename)] = src

        # Create a sorted list of (link, src) pairs, with longest paths before
        # short one. This ensures that 'foobar' will appear before 'foo'.
        link_to_source_list = sorted(
            link_to_source.items(), key=lambda x: x[0], reverse=True)

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
        'PATH': os.path.join(build_goroot, 'bin') + ':/usr/bin:/bin',
        # Disable modules to ensure Go doesn't try to download dependencies.
        'GO111MODULE': 'off',
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
    }

    # Infra sets $TMPDIR which is cleaned between builds.
    if os.getenv('TMPDIR'):
        env['TMPDIR'] = os.getenv('TMPDIR')

    if args.cgo:
        env['CGO_ENABLED'] = '1'
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
        cmd += ['build', '-trimpath']
    if args.verbose:
        cmd += ['-x']
    if args.tag:
        # Separate tags by spaces. This behavior is actually deprecated in the
        # go command line, but Fuchsia currently has an older version of go
        # that hasn't switched to commas.
        cmd += ['-tags', ' '.join(args.tag)]
    if args.buildmode:
        cmd += ['-buildmode', args.buildmode]
    if args.gcflag:
        cmd += ['-gcflags', ' '.join(args.gcflag)]
    if args.ldflag:
        cmd += ['-ldflags=' + ' '.join(args.ldflag)]
    cmd += [
        '-pkgdir',
        os.path.join(project_path, 'pkg'),
        '-o',
        args.output_path,
        args.package,
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
    if retcode == 0 and args.dump_syms and supports_build_id:
        if args.current_os == 'fuchsia':
            with open(dist + '.sym', 'w') as f:
                retcode = subprocess.call(
                    [args.dump_syms, '-r', '-o', 'Fuchsia', args.output_path],
                    stdout=f)

    if retcode == 0 and args.buildidtool and supports_build_id:
        if not args.build_id_dir:
            raise ValueError('Using --buildidtool requires --build-id-dir')
        retcode = subprocess.call(
            [
                args.buildidtool,
                '-build-id-dir',
                args.build_id_dir,
                '-stamp',
                dist + '.build-id.stamp',
                '-entry',
                '.debug=' + args.output_path,
                '-entry',
                '=' + dist,
            ])

    if retcode == 0:
        if args.depfile is not None:
            godepfile_args = [args.godepfile, '-o', dist]
            for f, t in link_to_source_list:
                godepfile_args += ['-prefixmap', '%s=%s' % (f, t)]
            if args.is_test:
                godepfile_args += ['-test']
            godepfile_args += [args.package]
            with open(args.depfile, 'wb') as into:
                subprocess.check_call(godepfile_args, env=env, stdout=into)

            if args.check_sources:
                try:
                    with open(args.source_list_path, 'r') as file:
                        sources = file.readlines()
                        verify_args = [
                            args.verify_depfile_script,
                            '-t',
                            args.gn_target_name,
                            '-d',
                            args.depfile,
                        ] + sources
                        subprocess.check_call(verify_args, env=env)
                except FileNotFoundError:
                    # TODO(fxb/58776): This will be an error when source listings are enforced.
                    print(
                        'Could not find source list file: ' +
                        args.source_list_path)
                    return 1

    return retcode


if __name__ == '__main__':
    sys.exit(main())
