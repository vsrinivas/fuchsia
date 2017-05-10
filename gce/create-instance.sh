#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z $FUCHSIA_GCE_PROJECT ]]; then
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh
fi

gcloud -q compute disks create "$FUCHSIA_GCE_DISK" --image "$FUCHSIA_GCE_IMAGE" || exit
gcloud -q compute instances create "$FUCHSIA_GCE_INSTANCE" --image "$FUCHSIA_GCE_GRUB" || exit
gcloud -q compute instances attach-disk "$FUCHSIA_GCE_INSTANCE" --disk "$FUCHSIA_GCE_DISK" || exit
gcloud -q compute instances add-metadata "$FUCHSIA_GCE_INSTANCE" --metadata=serial-port-enable=1 || exit
gcloud -q compute instances reset "$FUCHSIA_GCE_INSTANCE"
