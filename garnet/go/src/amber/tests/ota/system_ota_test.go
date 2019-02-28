// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ota

import (
	"bytes"
	"log"
	"testing"

	"system_ota/amber"
	"system_ota/device"
)

const (
	remotePackageDataPath = "/pkgfs/packages/ota-test-package/0/bin/app"
	remoteSystemDataPath  = "/system/bin/ota-test-app"
)

func TestPackageInstall(t *testing.T) {
	// Serve the repository before the test begins.
	go amber.ServeRepository(*repoDir)

	device, err := device.NewClient(*deviceHostname, *sshKeyFile)
	if err != nil {
		log.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	// Wait for the device to come online.
	device.WaitForDeviceToBeUp(t)

	// Tell the device to connect to our repository.
	device.RegisterAmberSource(*repoDir, *localHostname)

	// Get the device's current /boot/config/demvgr. Error out if it is the
	// same version we are about to OTA to.
	remoteDevmgrConfig := device.GetCurrentDevmgrConfig(t)
	if bytes.Equal(localDevmgrConfig, remoteDevmgrConfig) {
		t.Fatalf("system version should not be:\n\n%s", remoteDevmgrConfig)
	}

	// Verify the package is not installed.
	log.Printf("checking %q does not exist", remotePackageDataPath)
	if device.RemoteFileExists(t, remotePackageDataPath) {
		t.Fatalf("%q should not exist", remotePackageDataPath)
	}

	// Verify the system image file is not installed.
	log.Printf("checking %q does not exist", remoteSystemDataPath)
	if device.RemoteFileExists(t, remoteSystemDataPath) {
		t.Fatalf("%q should not exist", remoteSystemDataPath)
	}

	// Start the system OTA process.
	log.Printf("starting system OTA")
	device.TriggerSystemOTA(t)

	// At the this point the system should have been updated to the target
	// system version. Confirm the update by fetching the device's current
	// /boot/config/demvgr, and making sure it is the correct version.
	remoteDevmgrConfig = device.GetCurrentDevmgrConfig(t)
	if !bytes.Equal(localDevmgrConfig, remoteDevmgrConfig) {
		t.Fatalf("system version expected to be:\n\n%s\n\nbut instead got:\n\n%s", localDevmgrConfig, remoteDevmgrConfig)
	}

	// Verify the package is installed.
	log.Printf("checking %q exists", remotePackageDataPath)
	if !device.RemoteFileExists(t, remotePackageDataPath) {
		t.Fatalf("%q should exist", remotePackageDataPath)
	}

	// Verify the system image test file is installed.
	log.Printf("checking %q exist", remoteSystemDataPath)
	if !device.RemoteFileExists(t, remoteSystemDataPath) {
		t.Fatalf("%q should exist", remoteSystemDataPath)
	}

	log.Printf("done!")
}
