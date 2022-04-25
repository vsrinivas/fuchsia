#!/usr/bin/env bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

echo 'Updating goldens...'

if ! fx go test ./tools/integration/testsharder/cmd -update-goldens; then
	echo 'If this failure is unexpected, try running `fx setup-go` and retry.'
	exit 1
fi
