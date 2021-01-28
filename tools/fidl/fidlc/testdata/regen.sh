#!/usr/bin/env bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

echo "This script is deprecated in favor of fx regen-goldens"

script_dir=$(dirname "$0")

set -x
fx regen-goldens fidlc
