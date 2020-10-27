#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

${FUCHSIA_DIR}/prebuilt/third_party/go/linux-x64/bin/go run \
  ${FUCHSIA_DIR}/src/tests/benchmarks/fidl/fidlc/gen_benchmarks.go \
  ${FUCHSIA_DIR}/src/tests/benchmarks/fidl/fidlc/benchmarks.h