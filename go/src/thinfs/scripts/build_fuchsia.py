#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Top-level build script for ThinFS.
# Build ThinFS targetting Fuchsia.

import argparse
import os
import sys
from subprocess import call


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fuchsia-root', help='Path to root of Fuchsia project',
                        required=True)
    parser.add_argument('--go-binary', help='Path to go binary which can compile Fuchsia programs',
                        required=True)
    args = parser.parse_args()

    scripts_dir = os.path.dirname(os.path.realpath(__file__))
    os.environ['CGO_ENABLED'] = '1'
    os.environ['GOPATH'] = os.path.join(args.fuchsia_root, 'go')
    # TODO(smklein): Remove this usage of gccwrap once we've converted to building
    # the go runtime with clangwrap.
    os.environ['MAGENTA'] = os.path.join(args.fuchsia_root, 'magenta')
    os.environ['CC'] = os.path.join(args.fuchsia_root, 'third_party/go/misc/fuchsia/gccwrap.sh')
    os.environ['GOOS'] = 'fuchsia'
    output = 'thinfs'
    target = os.path.join(scripts_dir, '../magenta/main.go')
    return call([args.go_binary, 'build', '-o', output, target], env=os.environ)


if __name__ == '__main__':
    main()
