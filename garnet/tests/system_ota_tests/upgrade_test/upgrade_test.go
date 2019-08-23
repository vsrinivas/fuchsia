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
	"sync"
	"testing"

	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/paver"
	"fuchsia.googlesource.com/host_target_testing/util"

	"golang.org/x/crypto/ssh"
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

	t.Run("NPrimeThenN", func(t *testing.T) {
		log.Printf("starting N -> N' test")
		doSystemPrimeOTA(t, upgradeRepo)

		log.Printf("starting N' -> N test")
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

func doSystemPrimeOTA(t *testing.T, repo *packages.Repository) {
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

	expectedSystemImageMerkle, err := extractUpdateContentPackageMerkle(repo, "update_prime/0", "system_image_prime/0")

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

	// Since we're invoking system_updater.cmx directly, we need to do the GC ourselves
	err = device.Run("rmdir /pkgfs/ctl/garbage", os.Stdout, os.Stderr)
	if err != nil {
		t.Fatalf("error running GC: %v", err)
	}

	var wg sync.WaitGroup
	device.RegisterDisconnectListener(&wg)

	err = device.Run(`run "fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx" --update "fuchsia-pkg://fuchsia.com/update_prime" && sleep 60`, os.Stdout, os.Stderr)
	if err != nil {
		if _, ok := err.(*ssh.ExitMissingError); !ok {
			t.Fatalf("failed to run system_updater.cmx: %s", err)
		}
	}

	// Wait until we get a signal that we have disconnected
	wg.Wait()

	device.WaitForDeviceToBeUp(t)

	remoteSystemImageMerkle, err = device.GetSystemImageMerkle()
	if err != nil {
		t.Fatal(err)
	}

	if expectedSystemImageMerkle != remoteSystemImageMerkle {
		t.Fatalf("system version expected to be:\n\n%q\n\nbut instead got:\n\n%q", expectedSystemImageMerkle, remoteSystemImageMerkle)
	}

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
	return extractUpdateContentPackageMerkle(repo, "update/0", "system_image/0")
}

func extractUpdateContentPackageMerkle(repo *packages.Repository, updatePackageName string, contentPackageName string) (string, error) {
	// Extract the "packages" file from the "update" package.
	p, err := repo.OpenPackage(updatePackageName)
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

	merkle, ok := packages[contentPackageName]
	if !ok {
		return "", fmt.Errorf("could not find %s merkle", contentPackageName)
	}

	return merkle, nil
}
