// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package simulator

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/recovery/simulator/support"
	"go.fuchsia.dev/fuchsia/src/testing/emulator"
)

// TestUnpack checks that we can unpack emulator.
func TestBoot(t *testing.T) {
	distro, err := emulator.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()

	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(emulator.Params{
		Arch: arch,
		ZBI:  support.ZbiPath(t),
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("recovery: started")
}
