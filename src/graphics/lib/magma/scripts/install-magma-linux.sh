# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#!/bin/sh
set -ex

# This script builds and installs a "Magma SDK for Linux", comprised of a static library and
# a few headers.

# Download fuchsia
curl -s "https://fuchsia.googlesource.com/fuchsia/+/HEAD/scripts/bootstrap?format=TEXT" | base64 --decode | bash

pushd fuchsia
scripts/fx set core.x64 --release --no-goma
scripts/fx build src/graphics/lib/magma/src/libmagma_linux:libmagma_linux_x64_shared
popd

mkdir -p install/include install/lib
cp -v fuchsia/src/graphics/lib/magma/include/magma/*.h install/include
cp -v fuchsia/src/graphics/drivers/msd-intel-gen/include/magma_intel_gen_defs.h install/include
cp -v fuchsia/out/default/linux_x64-shared/obj/src/graphics/lib/magma/src/libmagma_linux/libmagma_linux.a install/lib

rm -rf fuchsia
