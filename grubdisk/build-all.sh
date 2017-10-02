#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh

set -e

case "$1" in
  -h|--help)
    echo "Usage: $(basename $0) [disk-path]" >&2
    exit 1
    ;;
esac

[[ -f "$FUCHSIA_GRUB_DIR/bin/grub-mkimage" ]] || "$FUCHSIA_GRUB_SCRIPTS_DIR/build-grub.sh"
[[ -f "$FUCHSIA_GRUB_DIR/core.img" ]] || "$FUCHSIA_GRUB_SCRIPTS_DIR/build-core.sh"

disk="${1:-"$FUCHSIA_OUT_DIR/grub.raw"}"

if [[ ! -f $disk ]]; then
  case $(uname) in
    Linux)
      fallocate -l 500k "$disk"
      ;;
    Darwin)
      mkfile -nv 500k "$disk"
      ;;
    *)
      head -c $((500*1024)) /dev/zero > "$disk"
      ;;
  esac
fi

"$FUCHSIA_GRUB_SCRIPTS_DIR/install.sh" "$disk" || exit 1

echo "Grub installed to $disk"
