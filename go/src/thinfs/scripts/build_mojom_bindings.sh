#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -eu

# Generate mojom bindings first for thinfs...
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo
echo -n "Generating Mojoms from ThinFS's interfaces... "
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py interfaces/filesystem/filesystem.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src
echo "Done"

# ... and then for mojo.
cd $MOJO_DIR/src
echo -n "Generating Mojoms from Mojo's interfaces... "
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py \
             mojo/public/interfaces/application/application.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py \
             mojo/public/interfaces/bindings/service_describer.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py \
             mojo/public/interfaces/bindings/mojom_types.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src
echo "Done"
