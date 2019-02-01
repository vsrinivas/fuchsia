#!/boot/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script wraps cobalt_testapp in a way that's suitable to run in
# continuous integration jobs, by disabling the network for testing.

set -e

/pkgfs/packages/cobalt_tests/0/bin/cobalt_testapp --no_network_for_testing
