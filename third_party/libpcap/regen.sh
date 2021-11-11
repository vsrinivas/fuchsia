#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$FUCHSIA_DIR"/tools/devshell/lib/vars.sh
source "$FUCHSIA_DIR"/tools/devshell/lib/prebuilt.sh

set -euxo pipefail

readonly REPO_DIR="$FUCHSIA_DIR/third_party/libpcap"
LIBPCAP_TAG="libpcap-$(cat "$REPO_DIR/src/VERSION")"
readonly LIBPCAP_TAG

"$FUCHSIA_DIR"/scripts/autoconf/regen.sh \
  OUT_CONFIG_H="$REPO_DIR/config.h" \
  REPO_ZIP_URL="https://github.com/the-tcpdump-group/libpcap/archive/refs/tags/$LIBPCAP_TAG.zip" \
  REPO_EXTRACTED_FOLDER="libpcap-$LIBPCAP_TAG"
