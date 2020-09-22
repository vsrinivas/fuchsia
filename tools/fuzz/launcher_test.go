// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"os"
	"os/exec"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

func TestQemuLauncherHandle(t *testing.T) {
	launcher := &QemuLauncher{Pid: 4141, TmpDir: "/tmp/woof"}

	handle, err := NewHandleFromObjects(launcher)
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
	}

	// Note: we don't serialize here because that is covered by handle tests

	build, _ := newMockBuild()

	reloadedLauncher, err := loadLauncherFromHandle(build, handle)
	if err != nil {
		t.Fatalf("error loading launcher from handle: %s", err)
	}

	ql, ok := reloadedLauncher.(*QemuLauncher)
	if !ok {
		t.Fatalf("incorrect launcher type")
	}

	if diff := cmp.Diff(launcher, ql, cmpopts.IgnoreUnexported(QemuLauncher{})); diff != "" {
		t.Fatalf("incorrect data in reloaded launcher (-want +got):\n%s", diff)
	}
}

func TestQemuLauncherWithMissingDeps(t *testing.T) {
	// Enable subprocess mocking
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	for _, dep := range []string{"zbi", "fvm", "blk", "zbitool", "qemu", "kernel", "authkeys"} {
		// This awkward error-discarding and casting is due to the need for the
		// signature of newMockBuild to match the other Build-creating functions
		build, _ := newMockBuild()
		brokenBuild := build.(*mockBuild)
		defer os.RemoveAll(brokenBuild.enableQemu(t, FakeQemuNormal))

		brokenBuild.paths[dep] = invalidPath
		launcher := NewQemuLauncher(build)
		if _, err := launcher.Start(); err == nil {
			t.Fatalf("Expected error launching with missing %q dep but succeeded", dep)
		}
	}
}

func TestQemuLauncherWithQemuFailure(t *testing.T) {
	// Enable subprocess mocking
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	build, _ := newMockBuild()
	defer os.RemoveAll(build.(*mockBuild).enableQemu(t, FakeQemuFailing))

	launcher := NewQemuLauncher(build)
	if _, err := launcher.Start(); err == nil {
		t.Fatalf("Expected error launching with failing qemu but succeeded")
	}
}

func TestQemuLauncherWithQemuTimeout(t *testing.T) {
	// Enable subprocess mocking
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	build, _ := newMockBuild()
	defer os.RemoveAll(build.(*mockBuild).enableQemu(t, FakeQemuSlow))

	launcher := NewQemuLauncher(build)
	launcher.timeout = 10 * time.Millisecond
	if _, err := launcher.Start(); err == nil {
		t.Fatalf("Expected error launching with slow qemu but succeeded")
	}
}

func TestQemuLauncher(t *testing.T) {
	// Enable subprocess mocking
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	build, _ := newMockBuild()
	defer os.RemoveAll(build.(*mockBuild).enableQemu(t, FakeQemuNormal))

	launcher := NewQemuLauncher(build)
	if _, err := launcher.Start(); err != nil {
		t.Fatalf("Error starting instance: %s", err)
	}

	alive, err := launcher.IsRunning()
	if err != nil {
		t.Fatalf("Error checking instance status: %s", err)
	}

	if !alive {
		t.Fatalf("instance not alive when it should be")
	}

	if err := launcher.Kill(); err != nil {
		t.Fatalf("Error killing instance: %s", err)
	}

	alive, err = launcher.IsRunning()
	if err != nil {
		t.Fatalf("Error checking instance status: %s", err)
	}

	if alive {
		t.Fatalf("instance alive when it shouldn't be")
	}
}
