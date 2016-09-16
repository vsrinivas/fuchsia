#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -eu

# Build the block device application.
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo/apps/blockd
echo -n "Building block device application... "
GOPATH=$GOPATH:$MOJO_DIR CGO_CFLAGS=-I$MOJO_DIR/src \
             CGO_LDFLAGS="-L$MOJO_DIR/src/out/Release/obj/mojo -lsystem_thunks" go build \
            -tags 'fuchsia include_mojo_cgo' -buildmode=c-shared -o $MOJO_DIR/src/out/Release/blockd.mojo
echo "Done"

# Build the filesystem service application.
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo/apps/filesystem
echo -n "Building filesystem service application... "
GOPATH=$GOPATH:$MOJO_DIR CGO_CFLAGS=-I$MOJO_DIR/src\
             CGO_LDFLAGS="-L$MOJO_DIR/src/out/Release/obj/mojo -lsystem_thunks" go build \
            -tags 'fuchsia include_mojo_cgo' -buildmode=c-shared -o $MOJO_DIR/src/out/Release/fs.mojo
echo "Done"

# Build the filesystem client application.
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo/apps/fsclient
echo -n "Building filesystem client application... "
GOPATH=$GOPATH:$MOJO_DIR CGO_CFLAGS=-I$MOJO_DIR/src \
             CGO_LDFLAGS="-L$MOJO_DIR/src/out/Release/obj/mojo -lsystem_thunks" go build \
            -tags 'fuchsia include_mojo_cgo' -buildmode=c-shared -o $MOJO_DIR/src/out/Release/fsclient.mojo
echo "Done"
