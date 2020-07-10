// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/support"
)

// Test that "dm reboot-recovery" will reboot the system.  On a real system, "reboot-recovery" will
// reboot to the recovery partition.  However, in this test environment we have no recovery
// partition so the system will end up back where it started.
func TestDmRebootRecovery(t *testing.T) {
	support.RebootWithCommand(t, "dm reboot-recovery", support.CleanReboot)
}
