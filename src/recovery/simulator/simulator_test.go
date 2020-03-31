// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package simulator

import (
	"os"
	"path/filepath"
	"testing"

	"fuchsia.googlesource.com/testing/qemu"
)

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	// TODO(47555): get the path from a build API instead.
	return filepath.Join(exPath, "../obj/build/images/recovery/recovery-eng.zbi")
}

// TestUnpack checks that we can unpack qemu.
func TestBoot(t *testing.T) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()

	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(qemu.Params{
		Arch: arch,
		ZBI:  zbiPath(t),
		// This test uses additional memory on ASAN builds than normal.
		Memory: 3072,
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("recovery: started")
}
