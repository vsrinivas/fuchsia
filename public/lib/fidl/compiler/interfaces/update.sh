#!/bin/bash

set -e -u

# Go into the //lib/fidl directory.
cd $(dirname $0)/../..

# Assume we're using an x86-64 debug build.
OUT_DIR=$PWD/../../out/debug-x86-64
GEN_DIR=$OUT_DIR/gen

# Rebuild the bindings
ninja -C $OUT_DIR "../../lib/fidl/compiler/interfaces/fidl_files.fidl^" "../../lib/fidl/compiler/interfaces/fidl_types.fidl^"

# Copy generated Python bindings
cp $GEN_DIR/lib/fidl/compiler/interfaces/fidl*.py $PWD/compiler/legacy_generators/pylib/mojom/generate/generated/

# Copy fidl_types.core.go
cp $GEN_DIR/go/src/lib/fidl/compiler/interfaces/fidl_types/fidl_types.core.go go/src/fidl/compiler/generated/fidl_types/fidl_types.core.go

# Copy and modify fidl_files.core.go
sed -e 's,lib/fidl/compiler/interfaces/fidl_types,fidl/compiler/generated/fidl_types,' $GEN_DIR/go/src/lib/fidl/compiler/interfaces/fidl_files/fidl_files.core.go > go/src/fidl/compiler/generated/fidl_files/fidl_files.core.go

