#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z $FUCHSIA_GCE_PROJECT ]]; then
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"/env.sh
fi

MACHINE_TYPE=n2-standard-4
while [[ $# -gt 0 ]]; do
  case $1 in
    -m|--machine-type)
      MACHINE_TYPE="$2"
      shift
      shift
      ;;
    -*|--*)
      echo "Unknown argument $1"
      exit 1
      ;;
    *)
      echo "Positional args not supported"
      exit 1
      ;;
  esac
done

gcloud -q compute instances create "$FUCHSIA_GCE_INSTANCE" \
  --enable-nested-virtualization \
  --metadata=serial-port-enable=1 --image "${FUCHSIA_GCE_IMAGE}" \
  --machine-type=${MACHINE_TYPE} || exit $?
