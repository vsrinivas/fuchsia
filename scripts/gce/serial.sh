#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z $FUCHSIA_GCE_PROJECT ]]; then
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"/env.sh
fi

# Control Path can often get too long with this command.
# If you run into this, add ControlPath=none to your SSH config.
gcloud compute connect-to-serial-port $FUCHSIA_GCE_INSTANCE
