#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to regenerate config.h files for Fuchsia.

source "$FUCHSIA_DIR"/tools/devshell/lib/vars.sh
source "$FUCHSIA_DIR"/tools/devshell/lib/prebuilt.sh

set -euxo pipefail

FXSET_WITH_ADDITIONAL=""
FXBUILD_WITH_ADDITIONAL=""
CPPFLAGS_ADDITIONAL=""
LDFLAGS_ADDITIONAL=""

for ARGUMENT in "$@"
do
    KEY=$(echo "$ARGUMENT" | cut -f1 -d=)
    VALUE=$(echo "$ARGUMENT" | cut -f2- -d=)

    case "$KEY" in
      OUT_CONFIG_H) OUT_CONFIG_H="$VALUE" ;;
      FXSET_WITH_ADDITIONAL) FXSET_WITH_ADDITIONAL="$VALUE" ;;
      FXBUILD_WITH_ADDITIONAL) FXBUILD_WITH_ADDITIONAL="$VALUE" ;;
      CPPFLAGS_ADDITIONAL) CPPFLAGS_ADDITIONAL="$VALUE" ;;
      LDFLAGS_ADDITIONAL) LDFLAGS_ADDITIONAL="$VALUE" ;;
      REPO_ZIP_URL) REPO_URL="$VALUE" ;;
      REPO_EXTRACTED_FOLDER) REPO_EXTRACTED_FOLDER="$VALUE" ;;
      *)
        cat <<EOF
Variables:

  OUT_CONFIG_H                Path to the generated config.h (required)
  FXSET_WITH_ADDITIONAL       Additional args for fx set
  FXBUILD_WITH_ADDITIONAL     Additional args for fx build
  CPPFLAGS_ADDITIONAL         Addtional CPP flags (passed to the configure script)
  LDFLAGS_ADDITIONAL          Additional LD flags
  REPO_ZIP_URL                The URL for the upstream repo (required)
  REPO_EXTRACTED_FOLDER       The folder that the repo unzips to (required)
EOF
        exit 1
    esac
done

fx set core.qemu-x64 --auto-dir --args=build_sdk_archives=true $FXSET_WITH_ADDITIONAL
fx build sdk:zircon_sysroot $FXBUILD_WITH_ADDITIONAL

SYSROOT_ROOT="$(mktemp -d)"
readonly SYSROOT_ROOT
cleanup() {
  rm -rf "$SYSROOT_ROOT"
}
trap cleanup EXIT

readonly BUILD_DIR="$FUCHSIA_OUT_DIR/core.qemu-x64"
readonly SYSROOT_DIR="$SYSROOT_ROOT/arch/x64/sysroot"
readonly TARGET=x86_64-unknown-fuchsia

tar -C "$SYSROOT_ROOT" -xf "$BUILD_DIR/sdk/archive/zircon_sysroot.tar.gz" || true

TMP_REPO="$(mktemp -d)"
readonly TMP_REPO
cleanup() {
  rm -rf "$TMP_REPO"
}
trap cleanup EXIT

wget -O "$TMP_REPO/repo.zip" "$REPO_URL"
unzip "$TMP_REPO/repo.zip" -d "$TMP_REPO"
readonly TMP_REPO_EXTRACTED="$TMP_REPO/$REPO_EXTRACTED_FOLDER"

readonly CC="$PREBUILT_CLANG_DIR/bin/clang"
CC_INCLUDE=$("$CC" --print-file-name=include)
readonly CC_INCLUDE

autoreconf "$TMP_REPO_EXTRACTED"
cd "$TMP_REPO_EXTRACTED" && ./configure \
  --host $TARGET \
  CC="$CC" \
  CFLAGS="-target $TARGET -nostdinc -nostdlib" \
  CPPFLAGS="-I$SYSROOT_DIR/include -I$CC_INCLUDE $CPPFLAGS_ADDITIONAL" \
  LDFLAGS="-L$SYSROOT_DIR/lib -L$BUILD_DIR/x64-shared -lc $LDFLAGS_ADDITIONAL"

echo """// Copyright $(date +"%Y") The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
""" > "$OUT_CONFIG_H"
cat "$TMP_REPO_EXTRACTED/config.h" >> "$OUT_CONFIG_H"
