#! /bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This is a manual update file for bindgen.
set -euo pipefail
set -x

# Types for which to generate the bindings.  Expand this list if you need more.
# The syntax is regex.
readonly GENERATE_TYPES="UBool|UCalendar.*|UChar.*|UData.*|UDate|UDateFormat.*|UEnumeration.*|UErrorCode|UText.*"

# Functions for which to generate the bindings.  Expand this list if you need more.
readonly GENERATE_FUNCTIONS="u_.*|ucal_.*|udat_.*|udata_.*|uenum_.*|uloc_.*|utext_.*"

if [[ $(bindgen --version) != "bindgen 0.50.0"  ]]; then
  echo "Requires bindgen version 0.50.0: check the script if you think this should work"
  exit 1;
fi

icu-config --version || \
  (echo "The generator requires icu-config to be in PATH; see README.md"; exit 1)

bindgen \
  --default-enum-style=rust \
  --with-derive-default \
  --with-derive-hash \
  --with-derive-partialord \
  --with-derive-partialeq \
  --whitelist-type="${GENERATE_TYPES}" \
  --whitelist-function="${GENERATE_FUNCTIONS}" \
  --opaque-type="" \
  --output=lib.rs \
  wrapper.h \
  -- \
  $(icu-config --cppflags)
