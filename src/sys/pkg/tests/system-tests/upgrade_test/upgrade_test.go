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
	"time"

	"fuchsia.googlesource.com/host_target_testing/artifacts"
	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/sl4f"
	"fuchsia.googlesource.com/system_tests/check"
	"fuchsia.googlesource.com/system_tests/pave"
	"fuchsia.googlesource.com/system_tests/script"
)

var c *config

func TestMain(m *testing.M) {
	log.SetPrefix("upgrade-test: ")
	log.SetFlags(log.Ldate | log.Ltime | log.LUTC | log.Lshortfile)

	var err error
	c, err = newConfig(flag.CommandLine)
	if err != nil {
		log.Fatalf("failed to create config: %s", err)
	}

	flag.Parse()

	if err = c.validate(); err != nil {
		log.Fatalf("config is invalid: %s", err)
	}

	os.Exit(m.Run())
}

func TestOTA(t *testing.T) {
	ctx := context.Background()

	outputDir, cleanup, err := c.archiveConfig.OutputDir()
	if err != nil {
		log.Fatalf("failed to get output directory: %v", err)
	}
	defer cleanup()

	device, err := c.deviceConfig.NewDeviceClient(ctx)
	if err != nil {
		log.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	downgradeBuild, err := c.getDowngradeBuild(ctx, outputDir)
	if err != nil {
		log.Fatalf("failed to get downgrade build: %v", err)
	}

	upgradeBuild, err := c.getUpgradeBuild(ctx, outputDir)
	if err != nil {
		log.Fatalf("failed to get upgrade build: %v", err)
	}

	rpcClient, err := initializeDevice(ctx, device, downgradeBuild)
	if err != nil {
		t.Fatalf("Device failed to initialize: %v", err)
	}
	defer func() {
		if rpcClient != nil {
			rpcClient.Close()
		}
	}()

	testOTAs(ctx, device, upgradeBuild, &rpcClient)
}

func testOTAs(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
	rpcClient **sl4f.Client,
) {
	for i := 1; i <= c.cycleCount; i++ {
		log.Printf("OTA Attempt %d", i)

		if err := doTestOTAs(ctx, device, build, rpcClient); err != nil {
			log.Fatalf("OTA Attempt %d failed: %s", i, err)
		}
	}
}

func doTestOTAs(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
	rpcClient **sl4f.Client,
) error {
	log.Printf("Starting OTA test cycle. Time out in %s", c.cycleTimeout)

	startTime := time.Now()
	ctx, cancel := context.WithDeadline(ctx, startTime.Add(c.cycleTimeout))
	defer cancel()

	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return fmt.Errorf("error getting repository: %w", err)
	}

	// Install version N on the device if it is not already on that version.
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %s", err)
	}

	upToDate, err := check.IsDeviceUpToDate(ctx, device, expectedSystemImageMerkle)
	if err != nil {
		return fmt.Errorf("failed to check if device is up to date: %s", err)
	}
	if !upToDate {
		log.Printf("starting OTA from N-1 -> N test")
		otaTime := time.Now()
		if err := systemOTA(ctx, device, rpcClient, repo, true); err != nil {
			return fmt.Errorf("OTA from N-1 -> N failed: %s", err)
		}
		log.Printf("OTA from N-1 -> N successful in %s", time.Now().Sub(otaTime))
	}

	log.Printf("starting OTA N -> N' test")
	otaTime := time.Now()
	if err := systemPrimeOTA(ctx, device, rpcClient, repo, false); err != nil {
		return fmt.Errorf("OTA from N -> N' failed: %s", err)
	}
	log.Printf("OTA from N -> N' successful in %s", time.Now().Sub(otaTime))

	log.Printf("starting OTA N' -> N test")
	otaTime = time.Now()
	if err := systemOTA(ctx, device, rpcClient, repo, false); err != nil {
		return fmt.Errorf("OTA from N' -> N failed: %s", err)
	}
	log.Printf("OTA from N' -> N successful in %s", time.Now().Sub(otaTime))
	log.Printf("OTA cycle sucessful in %s", time.Now().Sub(startTime))

	return nil
}

func initializeDevice(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
) (*sl4f.Client, error) {
	log.Printf("Initializing device")

	startTime := time.Now()
	ctx, cancel := context.WithDeadline(ctx, startTime.Add(c.paveTimeout))
	defer cancel()

	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return nil, fmt.Errorf("error etting downgrade repository: %s", err)
	}

	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return nil, fmt.Errorf("error extracting expected system image merkle: %w", err)
	}

	if err := script.RunScript(ctx, device, repo, nil, c.beforeInitScript); err != nil {
		return nil, fmt.Errorf("failed to run before-init-script: %w", err)
	}

	if build != nil {
		if err := pave.PaveDevice(ctx, device, build, c.otaToRecovery); err != nil {
			return nil, fmt.Errorf("failed to pave device during initialization: %w", err)
		}
	}

	// Creating a sl4f.Client requires knowing the build currently running
	// on the device, which not all test cases know during start. Store the
	// one true client here, and pass around pointers to the various
	// functions that may use it or device.Client to interact with the
	// target. All OTA attempts must first Close and nil out an existing
	// rpcClient and replace it with a new one after reboot. The final
	// rpcClient, if present, will be closed by the defer here.
	var rpcClient *sl4f.Client
	var expectedConfig *sl4f.Configuration
	if build != nil {
		rpcClient, err = device.StartRpcSession(ctx, repo)
		if err != nil {
			return nil, fmt.Errorf("unable to connect to sl4f after pave: %w", err)
		}

		// We always boot into the A partition after a pave.
		config := sl4f.ConfigurationA
		expectedConfig = &config
	}

	if err := check.ValidateDevice(
		ctx,
		device,
		rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		true,
	); err != nil {
		rpcClient.Close()
		return nil, fmt.Errorf("failed to validate during initialization: %w", err)
	}

	if err := script.RunScript(ctx, device, repo, &rpcClient, c.afterInitScript); err != nil {
		return nil, fmt.Errorf("failed to run after-init-script: %w", err)
	}

	log.Printf("initialization successful in %s", time.Now().Sub(startTime))

	return rpcClient, nil
}

// FIXME(45156) In order to ease landing Omaha, we're temporarily disabling the
// OTA test from talking directly to the update system, and instead are just
// directly calling out to the `system_updater` like we do for the N->N'
// upgrade. We need to do this because Omaha doesn't have a way to customize
// which server we are talking to, so we wouldn't be able to tell it to upgrade
// to the version of Fuchsia we want to test.
//
// We can revert back to this code once we've figured out how to implement this
// customization.
func systemOTA(
	ctx context.Context,
	device *device.Client,
	rpcClient **sl4f.Client,
	repo *packages.Repository,
	checkABR bool,
) error {
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %s", err)
	}

	expectedConfig, err := check.DetermineTargetABRConfig(ctx, *rpcClient)
	if err != nil {
		return fmt.Errorf("error determining target config: %s", err)
	}

	upToDate, err := check.IsDeviceUpToDate(ctx, device, expectedSystemImageMerkle)
	if err != nil {
		return fmt.Errorf("failed to check if device is up to date: %s", err)
	}
	if upToDate {
		return fmt.Errorf("device already updated to the expected version %q", expectedSystemImageMerkle)
	}

	// Disconnect from sl4f since the OTA should reboot the device.
	if *rpcClient != nil {
		(*rpcClient).Close()
		*rpcClient = nil
	}

	log.Printf("Rebooting device")
	startTime := time.Now()

	if err := device.TriggerSystemOTA(ctx, repo); err != nil {
		return fmt.Errorf("OTA failed: %s", err)
	}

	log.Printf("OTA complete in %s", time.Now().Sub(startTime))
	log.Printf("Validating device")

	*rpcClient, err = device.StartRpcSession(ctx, repo)
	if err != nil {
		return fmt.Errorf("unable to connect to sl4f after OTA: %s", err)
	}
	if err := check.ValidateDevice(
		ctx,
		device,
		*rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		checkABR,
	); err != nil {
		return fmt.Errorf("failed to validate after OTA: %s", err)
	}

	if err := script.RunScript(ctx, device, repo, rpcClient, c.afterTestScript); err != nil {
		return fmt.Errorf("failed to run test script after OTA: %w", err)
	}

	return nil
}

/*
func systemOTA(ctx context.Context, device *device.Client, rpcClient **sl4f.Client, repo *packages.Repository, checkABR bool) error {
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %s", err)
	}

	return otaToPackage(
		ctx,
		device,
		rpcClient,
		repo,
		expectedSystemImageMerkle,
		"fuchsia-pkg://fuchsia.com/update",
		checkABR,
	)
}
*/

func systemPrimeOTA(ctx context.Context, device *device.Client, rpcClient **sl4f.Client, repo *packages.Repository, checkABR bool) error {
	expectedSystemImageMerkle, err := repo.LookupUpdatePrimeSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %s", err)
	}

	return otaToPackage(
		ctx,
		device,
		rpcClient,
		repo,
		expectedSystemImageMerkle,
		"fuchsia-pkg://fuchsia.com/update_prime",
		checkABR,
	)
}

func otaToPackage(
	ctx context.Context,
	device *device.Client,
	rpcClient **sl4f.Client,
	repo *packages.Repository,
	expectedSystemImageMerkle string,
	updatePackageUrl string,
	checkABR bool,
) error {
	expectedConfig, err := check.DetermineTargetABRConfig(ctx, *rpcClient)
	if err != nil {
		return fmt.Errorf("error determining target config: %s", err)
	}

	upToDate, err := check.IsDeviceUpToDate(ctx, device, expectedSystemImageMerkle)
	if err != nil {
		return fmt.Errorf("failed to check if device is up to date: %s", err)
	}
	if upToDate {
		return fmt.Errorf("device already updated to the expected version %q", expectedSystemImageMerkle)
	}

	if err := device.DownloadOTA(ctx, repo, updatePackageUrl); err != nil {
		return fmt.Errorf("failed to download OTA: %s", err)
	}

	// Disconnect from sl4f since the OTA should reboot the device.
	if *rpcClient != nil {
		(*rpcClient).Close()
		*rpcClient = nil
	}

	log.Printf("Rebooting device")
	startTime := time.Now()

	if err = device.Reboot(ctx); err != nil {
		return fmt.Errorf("device failed to reboot after OTA applied: %s", err)
	}

	log.Printf("Reboot complete in %s", time.Now().Sub(startTime))
	log.Printf("Validating device")

	*rpcClient, err = device.StartRpcSession(ctx, repo)
	if err != nil {
		return fmt.Errorf("unable to connect to sl4f after OTA: %s", err)
	}
	if err := check.ValidateDevice(
		ctx,
		device,
		*rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		checkABR,
	); err != nil {
		return fmt.Errorf("failed to validate after OTA: %s", err)
	}

	if err := script.RunScript(ctx, device, repo, rpcClient, c.afterTestScript); err != nil {
		return fmt.Errorf("failed to run test script after OTA: %w", err)
	}

	return nil
}
