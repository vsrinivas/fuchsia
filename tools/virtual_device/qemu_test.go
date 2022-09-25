// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package virtual_device

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/qemu"
)

// TODO(kjharland): Add E2E tests to verify that QEMU launches as expected.
func TestQEMUCommand(t *testing.T) {
	testImageManifest := build.ImageManifest{
		{Name: "qemu-kernel", Path: "/kernel", Type: "kernel"},
		{Name: "storage-full", Path: "/fvm", Type: "blk"},
		{Name: "zircon-a", Path: "/ramdisk", Type: "zbi"},
	}

	t.Run("errs on invalid inputs", func(t *testing.T) {
		if err := QEMUCommand(&qemu.QEMUCommandBuilder{}, nil, testImageManifest); err == nil {
			t.Errorf("QEMUCommand with nil FVD did not return an error")
		}
		if err := QEMUCommand(&qemu.QEMUCommandBuilder{}, Default(), nil); err == nil {
			t.Errorf("QEMUCommand with nil image manifest did not return an error")
		}
	})

	// Verifes the output when given the default FVD as input.
	t.Run("default", func(t *testing.T) {
		b := &qemu.QEMUCommandBuilder{}
		if err := QEMUCommand(b, Default(), testImageManifest); err != nil {
			t.Fatal(err)
		}

		b.SetBinary("qemu")
		gotArgs, err := b.Build()
		if err != nil {
			t.Fatalf("Build() error: %v", err)
		}

		wantArgs := []string{
			"qemu",
			"-kernel",
			"/kernel",
			"-initrd",
			"/ramdisk",
			"-object",
			"iothread,id=iothread-maindisk",
			"-drive",
			"id=maindisk,file=/fvm,format=raw,if=none,cache=unsafe,aio=threads",
			"-device",
			"virtio-blk-pci,drive=maindisk,iothread=iothread-maindisk",
			"-smp",
			"1",
			"-m",
			"1",
			"-machine",
			"q35",
			"-fw_cfg",
			"name=etc/sercon-port,string=0",
			"-cpu",
			"Skylake-Client,-check",
			"-net",
			"none",
			"-append",
			"kernel.serial=legacy",
		}

		if diff := cmp.Diff(gotArgs, wantArgs); diff != "" {
			t.Fatalf("got diff (+got,-want):\n%s", diff)
		}
	})
}
