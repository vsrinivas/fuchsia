// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package e2e

import (
	"context"
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"

	"cloud.google.com/go/storage"
	"go.fuchsia.dev/fuchsia/tools/fvdl/e2e/e2etest"
)

// readLatestVersion reads the F*_LINUX file from GCS which contains the released SDK version for F* branch.
func readLatestVersion(ctx context.Context, t *testing.T) string {
	client, err := storage.NewClient(ctx)
	if err != nil {
		t.Fatalf("Cannot create cloud storage client: err %s", err)
	}
	rc, err := client.Bucket("fuchsia").Object("development/F1_LINUX").NewReader(ctx)
	if err != nil {
		t.Fatalf("Cannot create cloud storage reader: err %s", err)
	}
	version, err := ioutil.ReadAll(rc)
	rc.Close()
	if err != nil {
		t.Fatalf("Cannot read from cloud storage: err %s", err)
	}
	t.Logf("Latest Version: %s", version)
	return strings.TrimSuffix(string(version), "\n")
}

// TestStartFVDLSDK_downloadFromGCS tests starting FEMU using fvdl using images downloaded from GCS.
func TestStartFVDLSDK_downloadFromGCS(t *testing.T) {
	t.Skip("Skipping due to downloading from GCS causing flakes.")
	setUp(t, false)
	ctx := context.Background()
	vdlOut := runVDLWithArgs(
		ctx,
		t,
		[]string{
			"--sdk", "start", "--nointeractive", "--headless", "-V",
			// Specify aemu and device_launcher path to prevent fvdl from downloading them from CIPD
			"-e", mustAbs(t, filepath.Join(emulatorPath, "emulator")),
			"-d", mustAbs(t, filepath.Join(deviceLauncher, "device_launcher")),
			"--sdk-version", readLatestVersion(ctx, t),
			"--gcs-bucket", "fuchsia",
			"--image-name", "qemu-x64",
			"--image-size", "10G",
		},
		false, // intree
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

func TestStartFVDLSDK_noDownload(t *testing.T) {
	setUp(t, false)
	ctx := context.Background()
	vdlOut := runVDLWithArgs(ctx, t, []string{
		"--sdk", "start", "--nointeractive", "--headless", "-V",
		// Specify aemu and device_launcher path to prevent fvdl from downloading them from CIPD
		"-e", mustAbs(t, filepath.Join(emulatorPath, "emulator")),
		"-d", mustAbs(t, filepath.Join(deviceLauncher, "device_launcher")),
		"--start-package-server",
		// Giving this a garbage value, to make sure the test didn't actually try to download sdk 'x.x.x' from GCS
		"--sdk-version", "x.x.x",
		"--gcs-bucket", "fuchsia",
		"--image-name", "qemu-x64",
		"--image-size", "10G",
		"--fvm-image", fvm,
		"--zbi-image", zbi,
		"--kernel-image", kernel,
		"--amber-files", amberFiles,
		"--image-architecture", *targetCPU},
		false, // intree
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
