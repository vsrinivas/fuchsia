// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/support"
)

// Test that "dm reboot" will reboot the system.
func TestDmReboot(t *testing.T) {
	support.RebootWithCommand(t, "dm reboot", support.CleanReboot)
}
