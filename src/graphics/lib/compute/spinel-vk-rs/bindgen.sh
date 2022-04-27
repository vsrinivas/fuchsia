#!/bin/bash

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

#
# Requires $FUCHSIA_DIR to be set
#
if [[ -z "$FUCHSIA_DIR" ]] ; then
  echo "FUCHSIA_DIR not set."
  exit 1
fi

#
# Go to the directory this script lives in
#
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd $SCRIPT_DIR

#
# Clean
#
rm -f wrapper.h

#
# All relative to compute/ dir
#
readonly COMPUTE_DIR=$FUCHSIA_DIR/src/graphics/lib/compute
readonly VULKAN_DIR=$FUCHSIA_DIR/prebuilt/third_party/vulkansdk/linux/x86_64/include

#
# Import <spinel/*.h>
#
readonly SPINEL_DIR=$COMPUTE_DIR/spinel2/include
cd $SPINEL_DIR
for header in $(find spinel -type f); do
  echo "#include <${header}>" >> $SCRIPT_DIR/wrapper.h
done

#
# Import <spinel/platforms/vk/*.h>
#
readonly SPINEL_VK_DIR=$COMPUTE_DIR/spinel2/platforms/vk/include
cd $SPINEL_VK_DIR
for header in $(find spinel -type f); do
  echo "#include <${header}>" >> $SCRIPT_DIR/wrapper.h
done

#
# Import "spinel_vk_rs.h"
#
echo "#include <spinel-rs-sys/spinel_vk_rs.h>" >> $SCRIPT_DIR/wrapper.h

#
# Allow only "spinel_xxx" symbols and "SPN_xxx" defines.
#
ALLOWLIST="(spinel|SPN)_.*$"

#
# Generate bindings
#
cd $SCRIPT_DIR

bindgen wrapper.h                       \
  --allowlist-function "$ALLOWLIST"     \
  --allowlist-type "$ALLOWLIST"         \
  --allowlist-var "$ALLOWLIST"          \
  --no-layout-tests                     \
  --constified-enum-module "$ALLOWLIST" \
  -o src/lib.rs                         \
  --                                    \
  -I $SPINEL_DIR                        \
  -I $SPINEL_VK_DIR                     \
  -I $COMPUTE_DIR                       \
  -I $VULKAN_DIR

TMP="$(mktemp)"

#
# 1. Prepend Copyright comment
# 2. #[allow] for various warnings we don't care about
#
cat >> "$TMP" <<EOF
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]

EOF

cat src/lib.rs >> "$TMP"
mv "$TMP" src/lib.rs
rustfmt src/lib.rs

#
# Clean
#
rm wrapper.h
