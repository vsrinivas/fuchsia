#!/bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(fxbug.dev/73858): Possibly replace this script with a GN bindgen target
#                        when there is in-tree support.

# Move to our source directory, that's where platgen.h will be.
cd "`dirname $0`"

# If $FUCHSIA_DIR isn't set, take a guess.
FUCHSIA_DIR=${FUCHSIA_DIR-`pwd`/../../../../..}

# If $RUST_BINDGEN isn't set, take a guess.
RUST_BINDGEN=${RUST_BINDGEN-`which bindgen`}

readonly OT_DIR=${FUCHSIA_DIR}/third_party/openthread
readonly OT_INCLUDE_DIR=${OT_DIR}/include
readonly PLATGEN_H=platgen.h
readonly NULL=

die() {
  echo ERROR: "$*" >&2
  exit 1
}

# Sanity check.
test -f "$OT_INCLUDE_DIR/openthread-config-fuchsia.h" || die "Cannot find openthread-config-fuchsia.h in $OT_INCLUDE_DIR"

test -x "$RUST_BINDGEN" || die "Cannot find rust utility 'bindgen'"

readonly RAW_LINES="// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]"

$RUST_BINDGEN \
	"${PLATGEN_H}" \
	-o src/bindings.rs \
	--raw-line "${RAW_LINES}" \
	--size_t-is-usize \
	--no-layout-tests \
	--with-derive-default \
	--allowlist-function "ot[A-Z].*" \
	--allowlist-var "OT_[A-Z].*" \
	--allowlist-var "OPENTHREAD_[A-Z].*" \
	--allowlist-var "SPINEL_[A-Z].*" \
	-- \
	-D 'OPENTHREAD_CONFIG_FILE=<openthread-config-fuchsia.h>' \
	-I "${OT_INCLUDE_DIR}" \
	-I "${OT_DIR}" \
	${NULL}

$RUST_BINDGEN \
	"${OT_DIR}/src/lib/spinel/spinel.h" \
	-o src/spinel.rs \
	--raw-line "${RAW_LINES}" \
	--size_t-is-usize \
	--no-layout-tests \
	--with-derive-default \
	--allowlist-var "SPINEL_[A-Z].*" \
	${NULL}

fx format-code

