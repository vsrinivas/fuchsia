#!/boot/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script wraps cobalt_testapp in a way that's suitable to run in
# in the stand-alone cobalt_client CI but not in the Garnet CI.
# Because we do not pass the flag --no_network_for_testing, the
# real network is used. See cobalt_testapp_no_environment.

set -e

/pkgfs/packages/cobalt_tests/0/bin/cobalt_testapp --skip_environment_test
