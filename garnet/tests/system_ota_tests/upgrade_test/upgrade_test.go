// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package upgrade

import (
	"bytes"
	"context"
	"flag"
	"log"
	"os"
	"testing"

	"fuchsia.googlesource.com/system_ota_tests/config"
	"fuchsia.googlesource.com/system_ota_tests/packages"
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

func TestDowngradeAndUpgrade(t *testing.T) {
	downgradeRepo, err := c.GetDowngradeRepository()
	if err != nil {
		t.Fatal(err)
	}

	upgradeRepo, err := c.GetUpgradeRepository()
	if err != nil {
		t.Fatal(err)
	}

	log.Printf("starting downgrade test")
	doSystemOTA(t, downgradeRepo)

	log.Printf("starting upgrade test")
	doSystemOTA(t, upgradeRepo)
}

func doSystemOTA(t *testing.T, repo *packages.Repository) {
	device, err := c.NewDeviceClient()
	if err != nil {
		t.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	// Wait for the device to come online.
	device.WaitForDeviceToBeUp(t)

	// Extract the "data/snapshot" file from the "build-info" package.
	p, err := repo.OpenPackage("/build-info/0")
	if err != nil {
		t.Fatal(err)
	}
	expectedBuildSnapshot, err := p.ReadFile("data/snapshot")
	if err != nil {
		t.Fatal(err)
	}

	// Tell the device to connect to our repository.
	localHostname, err := c.LocalHostname()
	if err != nil {
		t.Fatal(err)
	}

	// Serve the repository before the test begins.
	server, err := repo.Serve(localHostname)
	if err != nil {
		t.Fatal(err)
	}
	defer server.Shutdown(context.Background())

	device.RegisterPackageRepository(server)

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

	log.Printf("system OTA successful")
}
