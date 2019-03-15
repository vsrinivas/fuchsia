// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/system_ota_test/amber"
	"fuchsia.googlesource.com/system_ota_test/build"
	"fuchsia.googlesource.com/system_ota_test/device"
)

func DoUpgradeTest(args []string) {
	tmpdir, err := ioutil.TempDir("", "")
	if err != nil {
		log.Fatalf("failed to create a temporary directory: %s", err)
	}
	defer os.RemoveAll(tmpdir)

	device, err := device.NewClient(*deviceHostname, *sshKeyFile)
	if err != nil {
		log.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	// Wait for the device to come online.
	device.WaitForDeviceToBeUp()

	// Download the package repo and build-info/snapshot
	repoDir, expectedBuildSnapshot := getRepoAndSnapshot(tmpdir)

	// Serve the repository before the test begins.
	go amber.ServeRepository(repoDir)

	// Tell the device to connect to our repository.
	device.RegisterAmberSource(repoDir, *localHostname)

	// Get the device's current /boot/config/demvgr. Error out if it is the
	// same version we are about to OTA to.
	remoteBuildSnapshot := device.GetBuildSnapshot()
	if bytes.Equal(expectedBuildSnapshot, remoteBuildSnapshot) {
		log.Fatalf("device already updated to the expected version:\n\n%s", expectedBuildSnapshot)
	}

	// Start the system OTA process.
	log.Printf("starting system OTA")
	device.TriggerSystemOTA()

	// At the this point the system should have been updated to the target
	// system version. Confirm the update by fetching the device's current
	// /boot/config/demvgr, and making sure it is the correct version.
	remoteBuildSnapshot = device.GetBuildSnapshot()
	if !bytes.Equal(expectedBuildSnapshot, remoteBuildSnapshot) {
		log.Fatalf("system version expected to be:\n\n%s\n\nbut instead got:\n\n%s", expectedBuildSnapshot, remoteBuildSnapshot)
	}

	log.Printf("SUCCESS")
}

func getRepoAndSnapshot(dir string) (string, []byte) {
	// Connect to the build archive service.
	archive := build.NewArchive(*lkgbPath, *artifactsPath, dir)

	// Fetch the build id from the latest builder if one wasn't passed in.
	if *buildID == "" {
		id, err := archive.LookupBuildID(*builderName)
		if err != nil {
			log.Fatalf("failed to lookup build id: %s", err)
		}
		*buildID = id
	}

	// Extract out the current build-info/0 data/snapshot file.
	systemSnapshot, err := archive.GetSystemSnapshot(*buildID)
	if err != nil {
		log.Fatalf("failed to fetch the system snapshot: %s", err)
	}
	buildInfo, ok := systemSnapshot.Snapshot.Packages["build-info/0"]
	if !ok {
		log.Fatalf("cannot find build-info/0 package")
	}
	snapshotMerkle, ok := buildInfo.Files["data/snapshot"]
	if !ok {
		log.Fatalf("cannot find data/snapshot merkle")
	}
	log.Printf("snapshot merkle: %s", snapshotMerkle)

	// Download the packages
	packagesDir, err := archive.GetPackages(*buildID)
	if err != nil {
		log.Fatalf("failed to fetch the amber repository: %s", err)
	}
	repoDir := filepath.Join(packagesDir, "amber-files", "repository")

	expectedBuildSnapshot, err := ioutil.ReadFile(filepath.Join(repoDir, "blobs", snapshotMerkle))
	if err != nil {
		log.Fatalf("failed to read build-info snapshot file: %s", err)
	}

	return repoDir, expectedBuildSnapshot
}
