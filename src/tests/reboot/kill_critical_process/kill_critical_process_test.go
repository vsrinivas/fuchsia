// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/reboottest"
)

// Test that "killall bootsvc" will reboot the system (because bootsvc is
// marked as a critical process; see also |zx_job_set_critical|).
func TestKillCriticalProcess(t *testing.T) {
	// Killing a critical process will result in an "unclean reboot" because,
	// among other things, the filesystem won't be shutdown cleanly.
	reboottest.RebootWithCommand(t, "killall bootsvc", reboottest.UncleanReboot, "fuchsia.zbi")
}
