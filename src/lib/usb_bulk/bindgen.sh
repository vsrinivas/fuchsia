#!/bin/bash

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script generates Rust bindings for the usb_bulk C++ library.

# Determine paths for this script and its directory, and set $FUCHSIA_DIR.
readonly FULL_PATH="${BASH_SOURCE[0]}"
readonly SCRIPT_DIR="$(cd "$(dirname "${FULL_PATH}")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/../../../tools/devshell/lib/vars.sh"

set -eu

cd "${SCRIPT_DIR}"

readonly RELPATH="${FULL_PATH#${FUCHSIA_DIR}/}"
readonly BINDGEN="${PREBUILT_RUST_BINDGEN_DIR}/bindgen"

# Generate annotations for the top of the generated source file.
readonly RAW_LINES="// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated by ${RELPATH} using $("${BINDGEN}" --version)

// Allow non-conventional naming for imports from C/C++.
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
// TODO(https://github.com/rust-lang/rust-bindgen/issues/1651): Remove once bindgen is fixed.
#![cfg_attr(test, allow(deref_nullptr))]

// This attribute ensures proper linkage. Applying it to an empty block to satisfy
// linking requirements for later blocks is explicitly suggested by
// https://doc.rust-lang.org/reference/items/external-blocks.html#the-link-attribute.
#[link(name = \"usb_bulk\", kind = \"static\")]
extern \"C\" {}

// Configure linkage for MacOS.
#[cfg(target_os = \"macos\")]
#[link(name = \"IOKit\", kind = \"framework\")]
#[link(name = \"CoreFoundation\", kind = \"framework\")]
extern \"C\" {}"

"${BINDGEN}" \
    cpp/usb.h \
    --disable-header-comment \
    --raw-line "${RAW_LINES}" \
    --with-derive-default \
    --impl-debug \
    --output rust/src/usb.rs \
    --whitelist-function 'interface_(read|write|open|close)' \
    --whitelist-type '(ssize_t|usb_ifc_info|UsbInterface)' \
    -- -x c++
