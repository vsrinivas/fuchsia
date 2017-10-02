#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z $FUCHSIA_GCE_PROJECT ]]; then
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh
fi

$FUCHSIA_ROOT/scripts/gce/gce make-grub-image || exit 1

grubdisk="$FUCHSIA_OUT_DIR/${FUCHSIA_GCE_GRUB}.raw"

tmp="$(mktemp -d)"
if [[ ! -d $tmp ]]; then
  echo "mktemp failed" >&2
  exit 1
fi
trap "rm -rf '$tmp'" EXIT

mv "$grubdisk" "$tmp/disk.raw"
cd "$tmp"
tar -Sczf "$FUCHSIA_OUT_DIR/$FUCHSIA_GCE_GRUB.tar.gz" disk.raw
gsutil cp "$FUCHSIA_OUT_DIR/$FUCHSIA_GCE_GRUB.tar.gz" "gs://$FUCHSIA_GCE_PROJECT/$FUCHSIA_GCE_USER/$FUCHSIA_GCE_GRUB.tar.gz" || exit 1
gcloud -q compute images delete "$FUCHSIA_GCE_GRUB"
gcloud -q compute images create "$FUCHSIA_GCE_GRUB" --source-uri "gs://$FUCHSIA_GCE_PROJECT/$FUCHSIA_GCE_USER/$FUCHSIA_GCE_GRUB.tar.gz"
