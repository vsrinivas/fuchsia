#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/vars.sh

rm "${FUCHSIA_DIR}/.jiri_root/bin/fx"
ln -s "../../scripts/fx" "${FUCHSIA_DIR}/.jiri_root/bin/fx"
