// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package upgrade

import (
	"context"
	"flag"
	"io/ioutil"
	"log"
	"os"
	"sync"
	"testing"
	"time"

	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/sl4f"

	"golang.org/x/crypto/ssh"
)

var c *Config

func TestMain(m *testing.M) {
	var err error
	c, err = NewConfig(flag.CommandLine)
	if err != nil {
		log.Fatalf("failed to create config: %s", err)
	}

	flag.Parse()

	if err = c.Validate(); err != nil {
		log.Fatalf("config is invalid: %s", err)
	}

	os.Exit(m.Run())
}

func TestOTA(t *testing.T) {
	device, err := c.NewDeviceClient()
	if err != nil {
		t.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	// Creating a sl4f.Client requires knowing the build currently running
	// on the device, which not all test cases know during start. Store the
	// one true client here, and pass around pointers to the various
	// functions that may use it or device.Client to interact with the
	// targer. All OTA attempts must first Close and nil out an existing
	// rpcClient and replace it with a new one after reboot. The final
	// rpcClient, if present, will be closed by the defer here.
	var rpcClient *sl4f.Client
	defer func() {
		if rpcClient != nil {
			rpcClient.Close()
		}
	}()

	if c.ShouldRepaveDevice() {
		rpcClient = doPaveDevice(t, device)
	}

	if c.LongevityTest {
		doTestLongevityOTAs(t, device, &rpcClient)
	} else {
		doTestOTAs(t, device, &rpcClient)
	}
}

func doTestOTAs(t *testing.T, device *device.Client, rpcClient **sl4f.Client) {
	outputDir := c.OutputDir
	if outputDir == "" {
		var err error
		outputDir, err = ioutil.TempDir("", "system_ota_tests")
		if err != nil {
			t.Fatalf("failed to create a temporary directory: %s", err)
		}
		defer os.RemoveAll(outputDir)
	}

	repo, err := c.GetUpgradeRepository(outputDir)
	if err != nil {
		t.Fatal(err)
	}

	// Install version N on the device if it is not already on that version.
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		t.Fatalf("error extracting expected system image merkle: %s", err)
	}

	if !isDeviceUpToDate(t, device, *rpcClient, expectedSystemImageMerkle) {
		log.Printf("\n\n")
		log.Printf("starting OTA from N-1 -> N test")
		doSystemOTA(t, device, rpcClient, repo)
		log.Printf("OTA from N-1 -> N successful")
	}

	log.Printf("\n\n")
	log.Printf("starting OTA N -> N' test")
	doSystemPrimeOTA(t, device, rpcClient, repo)
	log.Printf("OTA from N -> N' successful")

	log.Printf("\n\n")
	log.Printf("starting OTA N' -> N test")
	doSystemOTA(t, device, rpcClient, repo)
	log.Printf("OTA from N' -> N successful")
}

func doTestLongevityOTAs(t *testing.T, device *device.Client, rpcClient **sl4f.Client) {
	builder, err := c.GetUpgradeBuilder()
	if err != nil {
		t.Fatal(err)
	}

	lastBuildID := ""
	attempt := 1
	for {

		log.Printf("Look up latest build for builder %s", builder)

		buildID, err := builder.GetLatestBuildID()
		if err != nil {
			t.Fatalf("error getting latest build for builder %s: %s", builder, err)
		}

		if buildID == lastBuildID {
			log.Printf("already updated to %s, sleeping", buildID)
			time.Sleep(60 * time.Second)
			continue
		}
		log.Printf("Longevity Test Attempt %d  upgrading from build %s to build %s", attempt, lastBuildID, buildID)

		doTestLongevityOTAAttempt(t, device, rpcClient, buildID)

		log.Printf("Longevity Test Attempt %d successful", attempt)
		log.Printf("------------------------------------------------------------------------------")

		lastBuildID = buildID
		attempt += 1
	}
}

func doTestLongevityOTAAttempt(t *testing.T, device *device.Client, rpcClient **sl4f.Client, buildID string) {
	// Use the output dir if specified, otherwise download into a temporary directory.
	outputDir := c.OutputDir
	if outputDir == "" {
		var err error
		outputDir, err = ioutil.TempDir("", "system_ota_tests")
		if err != nil {
			t.Fatalf("failed to create a temporary directory: %s", err)
		}
		defer os.RemoveAll(outputDir)
	}

	build, err := c.BuildArchive().GetBuildByID(buildID, outputDir)
	if err != nil {
		t.Fatalf("failed to find build %s: %s", buildID, err)
	}

	repo, err := build.GetPackageRepository()
	if err != nil {
		t.Fatalf("failed to get repo for build: %s", err)
	}

	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		t.Fatal(err)
	}

	if isDeviceUpToDate(t, device, *rpcClient, expectedSystemImageMerkle) {
		log.Printf("device already up to date")
		return
	}

	log.Printf("\n\n")
	log.Printf("OTAing to %s", build)
	doSystemOTA(t, device, rpcClient, repo)
}

func doPaveDevice(t *testing.T, device *device.Client) *sl4f.Client {
	// Use the output dir if specified, otherwise download into a temporary directory.
	outputDir := c.OutputDir
	if outputDir == "" {
		var err error
		outputDir, err = ioutil.TempDir("", "system_ota_tests")
		if err != nil {
			t.Fatalf("failed to create a temporary directory: %s", err)
		}
		defer os.RemoveAll(outputDir)
	}
	downgradePaver, err := c.GetDowngradePaver(outputDir)
	if err != nil {
		t.Fatal(err)
	}

	downgradeRepo, err := c.GetDowngradeRepository(outputDir)
	if err != nil {
		t.Fatal(err)
	}

	log.Printf("starting pave")

	expectedSystemImageMerkle, err := downgradeRepo.LookupUpdateSystemImageMerkle()
	if err != nil {
		t.Fatalf("error extracting expected system image merkle: %s", err)
	}

	// Reboot the device into recovery and pave it.
	if err = device.RebootToRecovery(); err != nil {
		t.Fatalf("failed to reboot to recovery: %s", err)
	}

	if err = downgradePaver.Pave(c.DeviceName); err != nil {
		t.Fatalf("device failed to pave: %s", err)
	}

	// Wait for the device to come online.
	device.WaitForDeviceToBeConnected()

	rpcClient, err := device.StartRpcSession(downgradeRepo)
	if err != nil {
		// FIXME(40913): every downgrade builder should at least build
		// sl4f as a universe package.
		log.Printf("unable to connect to sl4f after pave: %s", err)
		//t.Fatalf("unable to connect to sl4f after pave: %s", err)
	}

	validateDevice(t, device, rpcClient, downgradeRepo, expectedSystemImageMerkle)

	log.Printf("paving successful")

	return rpcClient
}

func doSystemOTA(t *testing.T, device *device.Client, rpcClient **sl4f.Client, repo *packages.Repository) {
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		t.Fatalf("error extracting expected system image merkle: %s", err)
	}

	server := setupOTAServer(t, device, *rpcClient, repo, expectedSystemImageMerkle)
	defer server.Shutdown(context.Background())

	if err := device.TriggerSystemOTA(repo, rpcClient); err != nil {
		t.Fatalf("OTA failed: %s", err)
	}

	log.Printf("OTA complete, validating device")
	validateDevice(t, device, *rpcClient, repo, expectedSystemImageMerkle)
}

func doSystemPrimeOTA(t *testing.T, device *device.Client, rpcClient **sl4f.Client, repo *packages.Repository) {
	expectedSystemImageMerkle, err := repo.LookupUpdatePrimeSystemImageMerkle()
	if err != nil {
		t.Fatalf("error extracting expected system image merkle: %s", err)
	}

	server := setupOTAServer(t, device, *rpcClient, repo, expectedSystemImageMerkle)
	defer server.Shutdown(context.Background())

	// Since we're invoking system_updater.cmx directly, we need to do the GC ourselves
	// FIXME(40913): every downgrade builder should at least build
	// sl4f as a universe package, which would ensure rpcClient is non-nil here.
	if *rpcClient == nil {
		err = device.DeleteRemotePath("/pkgfs/ctl/garbage")
	} else {
		err = (*rpcClient).FileDelete("/pkgfs/ctl/garbage")
	}
	if err != nil {
		t.Fatalf("error running GC: %v", err)
	}

	// In order to manually trigger the system updater, we need the `run`
	// package. Since builds can be configured to not automatically install
	// packages, we need to explicitly resolve it.
	err = device.Run("pkgctl resolve fuchsia-pkg://fuchsia.com/run/0", os.Stdout, os.Stderr)
	if err != nil {
		t.Fatalf("error resolving the run package: %v", err)
	}

	var wg sync.WaitGroup
	device.RegisterDisconnectListener(&wg)

	log.Printf("starting system OTA")

	err = device.Run(`run "fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx" --update "fuchsia-pkg://fuchsia.com/update_prime" && sleep 60`, os.Stdout, os.Stderr)
	if err != nil {
		if _, ok := err.(*ssh.ExitMissingError); !ok {
			t.Fatalf("failed to run system_updater.cmx: %s", err)
		}
	}

	// Wait until we get a signal that we have disconnected
	wg.Wait()

	device.WaitForDeviceToBeConnected()

	log.Printf("OTA complete, validating device")
	// FIXME: See comment in device.TriggerSystemOTA()
	if *rpcClient != nil {
		(*rpcClient).Close()
		*rpcClient = nil
	}
	*rpcClient, err = device.StartRpcSession(repo)
	if err != nil {
		t.Fatalf("unable to connect to sl4f after OTA: %s", err)
	}
	validateDevice(t, device, *rpcClient, repo, expectedSystemImageMerkle)
}

func setupOTAServer(t *testing.T, device *device.Client, rpcClient *sl4f.Client, repo *packages.Repository, expectedSystemImageMerkle string) *packages.Server {
	if isDeviceUpToDate(t, device, rpcClient, expectedSystemImageMerkle) {
		t.Fatalf("device already updated to the expected version %q", expectedSystemImageMerkle)
	}

	// Make sure the device doesn't have any broken static packages.
	if err := device.ValidateStaticPackages(); err != nil {
		t.Fatal(err)
	}

	// Tell the device to connect to our repository.
	localHostname, err := device.GetSshConnection()
	if err != nil {
		t.Fatal(err)
	}

	// Serve the repository before the test begins.
	server, err := repo.Serve(localHostname, "host_target_testing")
	if err != nil {
		t.Fatal(err)
	}

	if err := device.RegisterPackageRepository(server); err != nil {
		server.Shutdown(context.Background())
		t.Fatal(err)
	}

	return server
}

func isDeviceUpToDate(t *testing.T, device *device.Client, rpcClient *sl4f.Client, expectedSystemImageMerkle string) bool {
	// Get the device's current /system/meta. Error out if it is the same
	// version we are about to OTA to.
	var remoteSystemImageMerkle string
	var err error
	if rpcClient == nil {
		remoteSystemImageMerkle, err = device.GetSystemImageMerkle()
	} else {
		remoteSystemImageMerkle, err = rpcClient.GetSystemImageMerkle()
	}
	if err != nil {
		t.Fatal(err)
	}
	log.Printf("current system image merkle: %q", remoteSystemImageMerkle)
	log.Printf("upgrading to system image merkle: %q", expectedSystemImageMerkle)

	return expectedSystemImageMerkle == remoteSystemImageMerkle
}

func validateDevice(t *testing.T, device *device.Client, rpcClient *sl4f.Client, repo *packages.Repository, expectedSystemImageMerkle string) {
	// At the this point the system should have been updated to the target
	// system version. Confirm the update by fetching the device's current
	// /system/meta, and making sure it is the correct version.
	if !isDeviceUpToDate(t, device, rpcClient, expectedSystemImageMerkle) {
		t.Fatalf("system version failed to update to %q", expectedSystemImageMerkle)
	}

	// Make sure the device doesn't have any broken static packages.
	// FIXME(40913): every builder should at least build sl4f as a universe package.
	if rpcClient == nil {
		if err := device.ValidateStaticPackages(); err != nil {
			t.Fatal(err)
		}
	} else {
		if err := rpcClient.ValidateStaticPackages(); err != nil {
			t.Fatal(err)
		}
	}
}
