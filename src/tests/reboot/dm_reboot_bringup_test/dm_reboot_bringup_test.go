// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/support"
)

// Test that "dm reboot" will reboot the system.
// It's important to also test that bringup reboots cleanly because bringup doesn't have
// storage. Bringup must shutdown cleanly without some components that normally live in storage
// (like PowerManager).
func TestDmReboot(t *testing.T) {
	support.RebootWithCommandAndZbi(t, "dm reboot", support.CleanReboot, "bringup.zbi")
}
