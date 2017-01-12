#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build script for a Go app.

import argparse
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fuchsia-root', help='Path to root of Fuchsia project',
                        required=True)
    parser.add_argument('--root-out-dir', help='Path to root of build output',
                        required=True)
    parser.add_argument('--depfile', help='The path to the depfile',
                        required=True)
    parser.add_argument('--current-cpu', help='Target arch, x64 or arm64.',
                        required=True)
    parser.add_argument('package', help='The package name')
    args = parser.parse_args()

    if args.current_cpu == "x64":
        goarch = "amd64"
    elif args.current_cpu == "arm64":
        goarch = "arm64"
    else:
        print("unknown current_cpu: ", args.current_cpu)
        return 1

    sympaths = {
        'apps': 'apps',
        # TODO(crawshaw): consider moving thinfs to apps/thinfs
        'go/src/fuchsia.googlesource.com': 'fuchsia.googlesource.com',
    }

    go_binary = os.path.join(args.fuchsia_root, "third_party/go/bin/go")
    gopath = args.root_out_dir
    gopathsrc = os.path.join(gopath, "src")
    if not os.path.exists(gopathsrc):
        try:
            os.mkdir(gopathsrc)
        except OSError, e:
            # ignore, probably raced with another go build
            print("could not setup GOPATH symlink: ", e)
    for src in sympaths:
        dst = sympaths[src]
        try:
            os.symlink(os.path.join(args.fuchsia_root, src),
                       os.path.join(gopathsrc, dst))
        except OSError, e:
            if e.errno == os.errno.EEXIST:
                pass # ignore, already have symlink
            else:
                print("could not link: ", src, ": ", e)
                return 1

    os.environ['CGO_ENABLED'] = '1'
    os.environ['GOPATH'] = gopath
    # TODO(crawshaw): remove when clangwrap.sh/gccwrap.sh is clever.
    os.environ['MAGENTA'] = os.path.join(args.fuchsia_root, 'magenta')
    os.environ['GOOS'] = 'fuchsia'
    os.environ['GOARCH'] = goarch
    godepfile = os.path.join(args.fuchsia_root, 'buildtools/godepfile')
    pkgdir = os.path.join(args.root_out_dir, 'pkg')

    retcode = subprocess.call([go_binary, 'install', '-pkgdir', pkgdir, args.package],
                              env=os.environ)
    if retcode == 0:
        binname = os.path.basename(args.package)
        src = os.path.join(gopath, "bin/fuchsia_"+goarch+"/"+binname)
        dst = os.path.join(gopath, binname)
        os.rename(src, dst)
        if args.depfile is not None:
            with open(args.depfile, "wb") as out:
                os.environ['GOROOT'] = os.path.join(args.fuchsia_root, "third_party/go")
                subprocess.Popen([godepfile, '-o', binname, args.package], stdout=out, env=os.environ)
    return retcode


if __name__ == '__main__':
    sys.exit(main())
