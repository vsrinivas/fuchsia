#!/boot/bin/sh
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

run_integration_tests --test_file=/pkgfs/packages/modular_tests/0/data/modular_tests.json "$@"

