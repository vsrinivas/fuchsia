// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package upgrade

import (
	"bytes"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"testing"

	"fuchsia.googlesource.com/system_ota_tests/amber"
	"fuchsia.googlesource.com/system_ota_tests/config"
)

var c *config.Config

func TestMain(m *testing.M) {
	var err error
	c, err = config.NewConfig(flag.CommandLine)
	if err != nil {
		log.Fatalf("failed to create config: %s", err)
	}
	defer c.Close()

	flag.Parse()

	if err = c.Validate(); err != nil {
		log.Fatalf("config is invalid: %s", err)
	}

	os.Exit(m.Run())
}

func TestUpgrade(t *testing.T) {
	device, err := c.NewDeviceClient()
	if err != nil {
		t.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	// Wait for the device to come online.
	device.WaitForDeviceToBeUp(t)

	buildID, err := c.BuildID()
	if err != nil {
		t.Fatal(err)
	}

	// Download the package repo and build-info/snapshot
	repoDir, expectedBuildSnapshot, err := getRepoAndSnapshot(buildID)
	if err != nil {
		t.Fatal(err)
	}

	// Serve the repository before the test begins.
	port, err := amber.ServeRepository(t, repoDir)
	if err != nil {
		t.Fatal(err)
	}

	// Tell the device to connect to our repository.
	localHostname, err := c.LocalHostname()
	if err != nil {
		t.Fatal(err)
	}
	device.RegisterAmberSource(repoDir, localHostname, port)

	// Get the device's current /boot/config/demvgr. Error out if it is the
	// same version we are about to OTA to.
	remoteBuildSnapshot := device.GetBuildSnapshot(t)
	if bytes.Equal(expectedBuildSnapshot, remoteBuildSnapshot) {
		t.Fatalf("device already updated to the expected version:\n\n%s", expectedBuildSnapshot)
	}

	// Start the system OTA process.
	log.Printf("starting system OTA")
	device.TriggerSystemOTA(t)

	// At the this point the system should have been updated to the target
	// system version. Confirm the update by fetching the device's current
	// /boot/config/demvgr, and making sure it is the correct version.
	remoteBuildSnapshot = device.GetBuildSnapshot(t)
	if !bytes.Equal(expectedBuildSnapshot, remoteBuildSnapshot) {
		t.Fatalf("system version expected to be:\n\n%s\n\nbut instead got:\n\n%s", expectedBuildSnapshot, remoteBuildSnapshot)
	}
}

func getRepoAndSnapshot(buildID string) (string, []byte, error) {
	archive := c.BuildArchive()

	// Extract out the current build-info/0 data/snapshot file.
	systemSnapshot, err := archive.GetSystemSnapshot(buildID)
	if err != nil {
		return "", nil, fmt.Errorf("failed to fetch the system snapshot: %s", err)
	}
	buildInfo, ok := systemSnapshot.Snapshot.Packages["build-info/0"]
	if !ok {
		return "", nil, fmt.Errorf("cannot find build-info/0 package")
	}
	snapshotMerkle, ok := buildInfo.Files["data/snapshot"]
	if !ok {
		return "", nil, fmt.Errorf("cannot find data/snapshot merkle")
	}
	log.Printf("snapshot merkle: %s", snapshotMerkle)

	// Download the packages
	packagesDir, err := archive.GetPackages(buildID)
	if err != nil {
		return "", nil, fmt.Errorf("failed to fetch the amber repository: %s", err)
	}
	repoDir := filepath.Join(packagesDir, "amber-files", "repository")

	expectedBuildSnapshot, err := ioutil.ReadFile(filepath.Join(repoDir, "blobs", snapshotMerkle))
	if err != nil {
		return "", nil, fmt.Errorf("failed to read build-info snapshot file: %s", err)
	}

	return repoDir, expectedBuildSnapshot, nil
}
