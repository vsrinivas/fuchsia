#!/bin/bash

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
scripts/fx shell 'k zx hwd'
scripts/fx shell 'k zx mwd'
scripts/fx shell 'while [ true ]; do /system/test/vkreadback; k zx ps; done'
