#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z $FUCHSIA_GCE_PROJECT ]]; then
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh
fi

# ctlpath frequently gets too long here, so instead, just ssh without it.
# was: gcloud compute connect-to-serial-port $instance
ssh -S none -p 9600 $FUCHSIA_GCE_PROJECT.$FUCHSIA_GCE_ZONE.$FUCHSIA_GCE_INSTANCE.$FUCHSIA_GCE_USER@ssh-serialport.googleapis.com
