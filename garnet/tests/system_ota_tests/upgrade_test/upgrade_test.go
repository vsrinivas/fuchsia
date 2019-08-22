// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package upgrade

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"testing"

	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/paver"
	"fuchsia.googlesource.com/host_target_testing/util"
)

var c *Config

func TestMain(m *testing.M) {
	var err error
	c, err = NewConfig(flag.CommandLine)
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

func TestOTA(t *testing.T) {
	downgradePaver, err := c.GetDowngradePaver()
	if err != nil {
		t.Fatal(err)
	}

	downgradeRepo, err := c.GetDowngradeRepository()
	if err != nil {
		t.Fatal(err)
	}

	upgradeRepo, err := c.GetUpgradeRepository()
	if err != nil {
		t.Fatal(err)
	}

	t.Run("PaveThenUpgrade", func(t *testing.T) {
		log.Printf("starting downgrade pave")
		doPave(t, downgradePaver, downgradeRepo)

		log.Printf("starting upgrade test")
		doSystemOTA(t, upgradeRepo)
	})

	t.Run("DowngradeThenUpgrade", func(t *testing.T) {
		log.Printf("starting downgrade test")
		doSystemOTA(t, downgradeRepo)

		log.Printf("starting upgrade test")
		doSystemOTA(t, upgradeRepo)
	})
}

func doPave(t *testing.T, paver *paver.Paver, repo *packages.Repository) {
	device, err := c.NewDeviceClient()
	if err != nil {
		t.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	// Reboot the device into recovery and pave it.
	device.RebootToRecovery(t)

	if err = paver.Pave(c.DeviceName); err != nil {
		t.Fatalf("device failed to pave: %s", err)
	}

	// Wait for the device to come online.
	device.WaitForDeviceToBeUp(t)

	validateDevice(t, repo, device)

	log.Printf("pave successful")
}

func doSystemOTA(t *testing.T, repo *packages.Repository) {
	device, err := c.NewDeviceClient()
	if err != nil {
		t.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	// Wait for the device to come online.
	device.WaitForDeviceToBeUp(t)

	// Get the device's current /system/meta. Error out if it is the same
	// version we are about to OTA to.
	remoteSystemImageMerkle, err := device.GetSystemImageMerkle()
	if err != nil {
		t.Fatal(err)
	}
	log.Printf("current system image merkle: %q", remoteSystemImageMerkle)

	expectedSystemImageMerkle, err := extractUpdateSystemImage(repo)
	if err != nil {
		t.Fatalf("error extracting expected system image merkle: %s", err)
	}
	log.Printf("upgrading to system image merkle: %q", expectedSystemImageMerkle)

	if expectedSystemImageMerkle == remoteSystemImageMerkle {
		t.Fatalf("device already updated to the expected version:\n\n%q", expectedSystemImageMerkle)
	}
	if err != nil {
		t.Fatal(err)
	}

	// Make sure the device doesn't have any broken static packages.
	device.ValidateStaticPackages(t)

	// Tell the device to connect to our repository.
	localHostname, err := device.GetSshConnection()
	if err != nil {
		t.Fatal(err)
	}

	// Serve the repository before the test begins.
	server, err := repo.Serve(localHostname)
	if err != nil {
		t.Fatal(err)
	}
	defer server.Shutdown(context.Background())

	if err := device.RegisterPackageRepository(server); err != nil {
		t.Fatal(err)
	}

	// Start the system OTA process.
	log.Printf("starting system OTA")
	device.TriggerSystemOTA(t)

	validateDevice(t, repo, device)

	log.Printf("system OTA successful")
}

func validateDevice(t *testing.T, repo *packages.Repository, device *device.Client) {
	// At the this point the system should have been updated to the target
	// system version. Confirm the update by fetching the device's current
	// /system/meta, and making sure it is the correct version.
	remoteSystemImageMerkle, err := device.GetSystemImageMerkle()
	if err != nil {
		t.Fatal(err)
	}
	log.Printf("current system image merkle: %q", remoteSystemImageMerkle)

	expectedSystemImageMerkle, err := extractUpdateSystemImage(repo)
	if err != nil {
		t.Fatalf("error extracting expected system image merkle: %s", err)
	}
	log.Printf("upgrading to system image merkle: %q", expectedSystemImageMerkle)

	if expectedSystemImageMerkle != remoteSystemImageMerkle {
		t.Fatalf("system version expected to be:\n\n%q\n\nbut instead got:\n\n%q", expectedSystemImageMerkle, remoteSystemImageMerkle)
	}

	// Make sure the device doesn't have any broken static packages.
	device.ValidateStaticPackages(t)
}

func extractUpdateSystemImage(repo *packages.Repository) (string, error) {
	// Extract the "packages" file from the "update" package.
	p, err := repo.OpenPackage("update/0")
	if err != nil {
		return "", err
	}
	f, err := p.Open("packages")
	if err != nil {
		return "", err
	}

	packages, err := util.ParsePackageList(f)
	if err != nil {
		return "", err
	}

	merkle, ok := packages["system_image/0"]
	if !ok {
		return "", fmt.Errorf("could not find system_image/0 merkle")
	}

	return merkle, nil
}
