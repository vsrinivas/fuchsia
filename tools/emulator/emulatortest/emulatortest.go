// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package emulatortest wraps package emulator for use in unit tests.
package emulatortest

import (
	"context"
	"os"
	"os/exec"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

// Distribution wraps emulator.Distribution.
type Distribution struct {
	d *emulator.Distribution
	t *testing.T
}

// Creates reimplements emulator.UnpackFrom.
func UnpackFrom(t *testing.T, path string, distroParams emulator.DistributionParams) *Distribution {
	d, err := emulator.UnpackFrom(path, distroParams)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := d.Delete(); err != nil {
			t.Error(err)
		}
	})
	return &Distribution{d, t}
}

// Creates reimplements emulator.Distribution.
func (d *Distribution) CreateContext(ctx context.Context, fvd *fvdpb.VirtualDevice) *Instance {
	i, err := d.d.CreateContext(ctx, fvd)
	if err != nil {
		d.t.Fatal(err)
	}
	d.t.Cleanup(func() {
		if err := d.d.Delete(); err != nil {
			d.t.Error(err)
		}
	})
	return &Instance{i, d.t}
}

// RunNonInteractive reimplements emulator.Distribution.
func (d *Distribution) RunNonInteractive(toRun, hostPathMinfsBinary, hostPathZbiBinary string, fvd *fvdpb.VirtualDevice) (string, string) {
	log, logerr, err := d.d.RunNonInteractive(toRun, hostPathMinfsBinary, hostPathZbiBinary, fvd)
	if err != nil {
		d.t.Fatal(err)
	}
	return log, logerr
}

// TargetCPU reimplements emulator.Distribution.
func (d *Distribution) TargetCPU() emulator.Arch {
	t, err := d.d.TargetCPU()
	if err != nil {
		d.t.Fatal(err)
	}
	return t
}

// ResizeRawImage reimplements emulator.ResizeRawImage
//
// Caller is responsible for cleaning up the image file at the path returned from this method.
// Terminates the test if an error occurs during resizing.
func (d *Distribution) ResizeRawImage(imageName, hostPathFvmBinary string) (path string) {
	var err error
	if path, err = d.d.ResizeRawImage(imageName, hostPathFvmBinary); err != nil {
		d.t.Fatal(err)
	}
	return
}

// Instance wraps emulator.Instance.
type Instance struct {
	i *emulator.Instance
	t *testing.T
}

// Start reimplements emulator.Instance.
func (i *Instance) Start() {
	i.StartPiped(nil)
}

// StartPiped reimplements emulator.Instance.
func (i *Instance) StartPiped(piped *exec.Cmd) {
	if err := i.i.StartPiped(piped); err != nil {
		i.t.Fatal(err)
	}
}

// Wait reimplements emulator.Instance.
func (i *Instance) Wait() *os.ProcessState {
	p, err := i.i.Wait()
	if err != nil {
		i.t.Fatal(err)
	}
	return p
}

// RunCommand reimplements emulator.Instance.
func (i *Instance) RunCommand(cmd string) {
	if err := i.i.RunCommand(cmd); err != nil {
		i.t.Fatal(err)
	}
}

// WaitForLogMessage reimplements emulator.Instance.
func (i *Instance) WaitForLogMessage(msg string) {
	i.WaitForLogMessages([]string{msg})
}

// WaitForLogMessages reimplements emulator.Instance.
func (i *Instance) WaitForLogMessages(msgs []string) {
	if err := i.i.WaitForLogMessages(msgs); err != nil {
		i.t.Fatal(err)
	}
}

// WaitForAnyLogMessage reimplements emulator.Instance.
func (i *Instance) WaitForAnyLogMessage(msgs ...string) string {
	s, err := i.i.WaitForAnyLogMessage(msgs...)
	if err != nil {
		i.t.Fatal(err)
	}
	return s
}

// WaitForLogMessageAssertNotSeen reimplements emulator.Instance.
func (i *Instance) WaitForLogMessageAssertNotSeen(msg, notSeen string) {
	if err := i.i.WaitForLogMessageAssertNotSeen(msg, notSeen); err != nil {
		i.t.Fatal(err)
	}
}

// AssertLogMessageNotSeenWithinTimeout reimplements emulator.Instance.
func (i *Instance) AssertLogMessageNotSeenWithinTimeout(notSeen string, timeout time.Duration) {
	if err := i.i.AssertLogMessageNotSeenWithinTimeout(notSeen, timeout); err != nil {
		i.t.Fatal(err)
	}
}

// CaptureLinesContaining reimplements emulator.Instance
func (i *Instance) CaptureLinesContaining(msg string, stop string) []string {
	res, err := i.i.CaptureLinesContaining(msg, stop)
	if err != nil {
		i.t.Fatal(err)
	}
	return res
}
