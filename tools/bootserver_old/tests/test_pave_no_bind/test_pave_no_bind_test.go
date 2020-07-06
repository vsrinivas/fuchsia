// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/bootserver_old/tests"
)

func TestPaveNoBind(t *testing.T) {
	_, cleanup := bootserver.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")
	defer cleanup()

	// Test that advertise request is serviced and paving starts as netsvc.netboot=true
	bootserver.AttemptPaveNoBind(t, true)
}
