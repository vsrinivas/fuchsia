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
func readLatestVersion(t *testing.T) string {
	ctx := context.Background()
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

// TestStartFVDLSDK tests starting FEMU using fvdl using images downloaded from GCS.
func TestStartFVDLSDK(t *testing.T) {
	setUp(t, false)
	vdlOut := runVDLWithArgs(t, []string{
		"--sdk", "start", "--nointeractive", "--headless", "-V",
		// Specify aemu and device_launcher path to prevent fvdl from downloading them from CIPD
		"-e", filepath.Join(emulatorPath, "emulator"),
		"-d", filepath.Join(deviceLauncher, "device_launcher"),
		"--sdk-version", readLatestVersion(t),
		"--gcs-bucket", "fuchsia",
		"--image-name", "qemu-x64",
		"--image-size", "10G"},
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
