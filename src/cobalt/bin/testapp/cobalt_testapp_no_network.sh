#!/boot/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is no longer executed during normal tests, but is copied to the
# target as a stub that matches a cmx file. The cmx file specifies the program
# that is run, and it's arguments.

/bin/run-test-component fuchsia-pkg://fuchsia.com/cobalt_tests#meta/cobalt_testapp_no_network.cmx
