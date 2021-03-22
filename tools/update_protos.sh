#!/usr/bin/env bash

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script compiles the .proto files and copies the compiled versions
# from the build dir back to the source tree. It should be run whenever
# the .proto files are modified.

set -euo pipefail

# cd to fuchsia checkout root.
cd "$(dirname "${BASH_SOURCE[0]}")/.."

readonly TARGETS=$(git grep -oP '(?<=golden_go_proto\(")[a-z_]+(?="\))' -- '*BUILD.gn' |
  # TODO(https://fxbug.dev/72810): proto_library doesn't support grpc-go.
  grep -vF femu-grpc |
  sed 's|/BUILD.gn||')
echo "$TARGETS" | sed 's|^|--with |' | xargs -t scripts/fx set core.qemu-x64 --no-goma
echo "$TARGETS" | xargs -t scripts/fx ninja -k 0 -C out/default

echo 'Consulting GN...'
echo "$TARGETS" | xargs -I % scripts/fx gn desc out/default % deps | grep '_diff$' | while read -r target; do
  echo "Finding protos for ${target}..."
  scripts/fx gn desc out/default "$target" inputs | head -n2 | sed 's|^//||' | xargs -t cp
done
