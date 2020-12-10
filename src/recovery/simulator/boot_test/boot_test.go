// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package simulator

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
)

// TestUnpack checks that we can unpack emulator.
func TestBoot(t *testing.T) {
	exPath := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exPath, "test_data"), emulator.DistributionParams{Emulator: emulator.Qemu})
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := distro.Delete(); err != nil {
			t.Error(err)
		}
	}()

	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(emulator.Params{
		Arch: arch,
		// TODO(fxbug.dev/47555): get the path from a build API instead.
		ZBI: filepath.Join(exPath, "..", "obj", "build", "images", "recovery", "recovery-eng.zbi"),
	})

	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := i.Kill(); err != nil {
			t.Error(err)
		}
	}()

	if err = i.WaitForLogMessage("recovery: started"); err != nil {
		t.Fatal(err)
	}
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(ex)
}
