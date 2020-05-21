// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testutil

import (
	"syscall/zx"
	"testing"
	"unsafe"

	"gen/netstack/link/eth"
)

/// MakeEntryFifo creates a pair of handles to a FIFO of "depth" FifoEntry
/// elements for use in tests. The created handles are automatically closed on
/// test cleanup.
func MakeEntryFifo(t *testing.T, depth uint) (zx.Handle, zx.Handle) {
	t.Helper()
	var device, client zx.Handle
	if status := zx.Sys_fifo_create(depth, uint(unsafe.Sizeof(eth.FifoEntry{})), 0, &device, &client); status != zx.ErrOk {
		t.Fatalf("failed to create fake FIFO: %s", status)
	}
	t.Cleanup(func() {
		_ = device.Close()
		_ = client.Close()
	})
	return device, client
}
