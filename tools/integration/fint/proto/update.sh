#!/usr/bin/env bash

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script compiles the .proto files and copies the compiled versions
# from the build dir back to the source tree. It should be run whenever
# the .proto files are modified.

set -o errexit
set -o pipefail

# cd to fuchsia checkout root.
cd "$( dirname "${BASH_SOURCE[0]}" )/../../../.."

scripts/fx set core.x64 --with //tools/integration/fint:protos
scripts/fx ninja -C out/default tools/integration/fint:protos

cp out/default/gen/go-proto-gen/src/tools/integration/fint/proto/*.pb.go \
  tools/integration/fint/proto/
cp out/default/obj/tools/integration/fint/*.desc.pb \
  tools/integration/fint/proto/
