#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z $FUCHSIA_DIR ]]; then
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/../devshell/lib/vars.sh
fi

get-gcloud-config() {
  gcloud -q config get-value "$@" 2>/dev/null
}

makefile() {
  local size=$1
  local path=$2
  case $(uname) in
    Linux)
      fallocate -l "$size" "$path"
      ;;
    Darwin)
      mkfile -n "$size" "$path"
      ;;
    *)
      echo "Unsupported platform $(uname)" >&2
      exit 1
      ;;
  esac
}

FUCHSIA_GCE_PROJECT=${FUCHSIA_GCE_PROJECT:-$(get-gcloud-config project)}
FUCHSIA_GCE_ZONE=${FUCHSIA_GCE_ZONE:-$(get-gcloud-config compute/zone)}
FUCHSIA_GCE_USER=${FUCHSIA_GCE_USER:-"$USER"}
FUCHSIA_GCE_INSTANCE=${FUCHSIA_GCE_INSTANCE:-"$FUCHSIA_GCE_USER-fuchsia"}
FUCHSIA_GCE_IMAGE=${FUCHSIA_GCE_IMAGE:-"$FUCHSIA_GCE_INSTANCE-img"}
FUCHSIA_GCE_DISK=${FUCHSIA_GCE_DISK:-"$FUCHSIA_GCE_INSTANCE-disk"}
FUCHSIA_GCE_GRUB=${FUCHSIA_GCE_GRUB:-zircon-grub}

[[ -n $FUCHSIA_GCE_PROJECT ]] || (echo "Set a default gcloud config for project or set \$FUCHSIA_GCE_PROJECT" >&2 && exit 1)
[[ -n $FUCHSIA_GCE_ZONE ]] || (echo "Set a default gcloud config for compute zone or set \$FUCHSIA_GCE_ZONE" >&2 && exit 1)
