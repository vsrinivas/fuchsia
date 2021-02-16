// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/reboottest"
)

// Test that "dm reboot-bootloader" will reboot the system.
//
// On a real system, "reboot-bootloader" will reboot to the bootloader.
// However, in this test environment it will simply reboot and the system will
// end up back where it started.
func TestDmRebootBootloader(t *testing.T) {
	reboottest.RebootWithCommand(t, "dm reboot-bootloader", reboottest.CleanReboot, "fuchsia.zbi")
}
