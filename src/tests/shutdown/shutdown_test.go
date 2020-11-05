// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/qemu"
)

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../fuchsia.zbi")
}

func TestShutdown(t *testing.T) {
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
		Arch:             arch,
		ZBI:              zbiPath(t),
		AppendCmdline:    "devmgr.log-to-debuglog",
		DisableDebugExit: true,
	})

	err = i.Start()
	if err != nil {
		t.Fatal(err)
	}

	i.WaitForLogMessage("initializing platform")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("vc: Successfully attached")

	// Trigger a shutdown.
	i.RunCommand("dm shutdown")

	// Start a timer so we can abort the wait by explicitly killing. This will yield a nice error
	// from the wait command that we can detect.
	timer := time.AfterFunc(120*time.Second, func() {
		i.Kill()
	})

	// Cannot check for log messages as we are racing with the shutdown, and if qemu closes first
	// checking for log messages will panic, so we just wait.
	ps, err := i.Wait()
	timer.Stop()
	if err != nil {
		t.Fatal(err)
	}
	if !ps.Success() {
		t.Fatal("Failed to shutdown cleanly")
	}
}
