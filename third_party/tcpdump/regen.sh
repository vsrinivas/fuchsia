#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$FUCHSIA_DIR"/tools/devshell/lib/vars.sh
source "$FUCHSIA_DIR"/tools/devshell/lib/prebuilt.sh

set -euxo pipefail

readonly REPO_DIR="$FUCHSIA_DIR/third_party/tcpdump"
TCPDUMP_TAG="tcpdump-$(cat "$REPO_DIR/src/VERSION")"
readonly TCPDUMP_TAG

readonly CONFIG_H="$REPO_DIR/config.h"

"$FUCHSIA_DIR"/scripts/autoconf/regen.sh \
  OUT_CONFIG_H="$CONFIG_H" \
  FXSET_WITH_ADDITIONAL="--with=//third_party/libpcap" \
  FXBUILD_WITH_ADDITIONAL="third_party/libpcap" \
  CPPFLAGS_ADDITIONAL="-I$FUCHSIA_DIR/third_party/libpcap/src" \
  LDFLAGS_ADDITIONAL="-lpcap" \
  REPO_ZIP_URL="https://github.com/the-tcpdump-group/tcpdump/archive/refs/tags/$TCPDUMP_TAG.zip" \
  REPO_EXTRACTED_FOLDER="tcpdump-$TCPDUMP_TAG"

# Manually override some symbols we expose but don't implement.
for i in HAVE_{FORK,GETSERVENT}; do
  sed -i "s,^#define $i 1$,/* #undef $i */," "$CONFIG_H"
done
