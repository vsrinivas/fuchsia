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
    parser.add_argument('--go-tool', help='The go tool to use for builds')
    parser.add_argument('--binname', help='Output file')
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
        'apps/amber': 'github.com/flynn/go-tuf',
        'apps/thinfs': 'fuchsia.googlesource.com/thinfs',
        'lib/fidl/go/src/fidl' : 'fidl',
        'third_party/golang/crypto': 'golang.org/x/crypto',
        'third_party/golibs/github.com/dustin/go-humanize': 'github.com/dustin/go-humanize',
        'third_party/golibs/github.com/flynn/go-docopt': 'github.com/flynn/go-docopt',
        'third_party/golibs/github.com/golang/snappy': 'github.com/golang/snappy',
        'third_party/golibs/github.com/syndtr/goleveldb/leveldb': 'github.com/syndtr/goleveldb/leveldb',
        'third_party/golibs/github.com/tent/canonical-json-go': 'github.com/tent/canonical-json-go',
        'third_party/netstack': 'github.com/google/netstack'
    }

    gopath = args.root_out_dir
    for src in sympaths:
        dst = os.path.join(gopath, "src", sympaths[src])
        try:
            os.makedirs(os.path.dirname(dst))
        except OSError, e:
            if e.errno == os.errno.EEXIST:
                pass # ignore, already have directory
            else:
                print("could not mkdir for path: ", dst, ": ", e)
                return 1
        try:
            os.symlink(os.path.join(args.fuchsia_root, src), dst)
        except OSError, e:
            if e.errno == os.errno.EEXIST:
                pass # ignore, already have symlink
            else:
                print("could not link: ", src, ": ", e)
                return 1

    os.environ['CGO_ENABLED'] = '1'
    os.environ['GOPATH'] = gopath + ":" + os.path.join(args.root_out_dir, "gen/go")
    os.environ['GOOS'] = 'fuchsia'
    os.environ['GOARCH'] = goarch
    if 'GOBIN' in os.environ:
        del os.environ['GOBIN']
    if 'GOROOT' in os.environ:
        del os.environ['GOROOT']
    godepfile = os.path.join(args.fuchsia_root, 'buildtools/godepfile')

    retcode = subprocess.call([args.go_tool, 'install', args.package],
                              env=os.environ)
    if retcode == 0:
        binname = os.path.basename(args.package)
        src = os.path.join(gopath, "bin", "fuchsia_"+goarch, binname)
        if args.binname:
            dst = os.path.join(gopath, args.binname)
        else:
            dst = os.path.join(gopath, binname)
        os.rename(src, dst)
        if args.depfile is not None:
            with open(args.depfile, "wb") as out:
                os.environ['GOROOT'] = os.path.join(args.fuchsia_root, "third_party/go")
                subprocess.Popen([godepfile, '-o', binname, args.package], stdout=out, env=os.environ)
    return retcode


if __name__ == '__main__':
    sys.exit(main())
