#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

# Go to the source directory
cd "$(dirname ${BASH_SOURCE[0]})"

exec python3 -m generate $*