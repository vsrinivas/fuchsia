// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	bootservertest "go.fuchsia.dev/fuchsia/tools/bootserver_old/tests"
)

func TestPaveNoBind(t *testing.T) {
	_ = bootservertest.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")

	// Test that advertise request is serviced and paving starts as netsvc.netboot=true
	bootservertest.AttemptPaveNoBind(t, true)
}
