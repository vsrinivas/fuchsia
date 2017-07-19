#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z $FUCHSIA_GCE_PROJECT ]]; then
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh
fi

mfv=$FUCHSIA_BUILD_DIR/host_x64/make-fuchsia-vol

if [[ ! -x $mfv ]]; then
	echo "You need to build the 'make-fuchsia-vol' package" >&2
	exit 1
fi

diskimage="$FUCHSIA_OUT_DIR/$FUCHSIA_GCE_IMAGE.img"

# TODO(raggi): look at size that sys part needs to be and use that.
makefile 10g "$diskimage"

$mfv "$diskimage" || exit 1

tmp="$(mktemp -d)"
if [[ ! -d $tmp ]]; then
  echo "mktemp failed" >&2
  exit 1
fi
trap "rm -rf '$tmp'" EXIT

cd "$tmp"
mv "$diskimage" disk.raw

tar -Sczf "$FUCHSIA_OUT_DIR/$FUCHSIA_GCE_IMAGE.tar.gz" disk.raw
gsutil cp "$FUCHSIA_OUT_DIR/$FUCHSIA_GCE_IMAGE.tar.gz" "gs://$FUCHSIA_GCE_PROJECT/$FUCHSIA_GCE_USER/$FUCHSIA_GCE_IMAGE.tar.gz"
gcloud -q compute images delete "$FUCHSIA_GCE_IMAGE"
gcloud -q compute images create "$FUCHSIA_GCE_IMAGE" --source-uri "gs://$FUCHSIA_GCE_PROJECT/$FUCHSIA_GCE_USER/$FUCHSIA_GCE_IMAGE.tar.gz"
