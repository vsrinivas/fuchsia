#!/boot/bin/sh
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

/system/test/run_integration_tests --test_file=/system/test/modular_tests/modular_tests.json "$@"
