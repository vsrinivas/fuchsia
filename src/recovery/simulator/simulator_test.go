// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package simulator

import (
	"testing"

	"fuchsia.googlesource.com/testing/qemu"
)

// TestUnpack checks that we can unpack qemu.
func TestUnpack(t *testing.T) {
	err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
}
