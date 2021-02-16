// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/reboottest"
)

// Test that "dm reboot" will reboot the system.
func TestDmReboot(t *testing.T) {
	reboottest.RebootWithCommand(t, "dm reboot", reboottest.CleanReboot, "fuchsia.zbi")
}
