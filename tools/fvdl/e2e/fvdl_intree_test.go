// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package e2e

import (
	"context"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fvdl/e2e/e2etest"
)

func TestStartFVDLInTree_GrpcWebProxy(t *testing.T) {
	setUp(t, true)
	vdlOut := runVDLWithArgs(
		context.Background(),
		t,
		[]string{
			"start", "--nointeractive", "-V",
			"--grpcwebproxy", "0", "--image-size", "10G",
			// Note: for real in-tree use case, we'll default to parsing images.json file to locate
			// build artifacts. For this test, we'll specify the image files locations via cmd args.
			"--fvm-image", fvm,
			"--zbi-image", zbi,
			"--kernel-image", kernel,
			"--image-architecture", *targetCPU},
		true, // intree
	)
	pid := e2etest.GetProcessPID("Emulator", vdlOut)
	if len(pid) == 0 {
		t.Errorf("Cannot obtain Emulator info from vdl output: %s", vdlOut)
	} else if !e2etest.IsEmuRunning(pid) {
		t.Error("Emulator is not running")
	}
	if process := e2etest.GetProcessPID("grpcwebproxy", vdlOut); len(process) == 0 {
		t.Errorf("Cannot obtain grpcwebproxy process from vdl output: %s", vdlOut)
	}
	if port := e2etest.GetProcessPort("grpcwebproxy", vdlOut); len(port) == 0 {
		t.Errorf("Cannot obtain grpcwebproxy port from vdl output: %s", vdlOut)
	}
}

func TestStartFVDLInTree_Headless_ServePackages_Tuntap(t *testing.T) {
	setUp(t, true)
	vdlOut := runVDLWithArgs(
		context.Background(),
		t,
		[]string{
			"start", "--nointeractive", "-V", "--start-package-server",
			"--headless", "-N", "--image-size", "10G",
			"--fvm-image", fvm,
			"--zbi-image", zbi,
			"--kernel-image", kernel,
			"--image-architecture", *targetCPU},
		true, // intree
	)
	pid := e2etest.GetProcessPID("Emulator", vdlOut)
	if len(pid) == 0 {
		t.Errorf("Cannot obtain Emulator info from vdl output: %s", vdlOut)
	} else if !e2etest.IsEmuRunning(pid) {
		t.Error("Emulator is not running")
	}
	if process := e2etest.GetProcessPID("PackageServer", vdlOut); len(process) == 0 {
		t.Errorf("Cannot obtain PackageServer process from vdl output: %s", vdlOut)
	}
}
