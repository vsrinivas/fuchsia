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

	"golang.org/x/crypto/ssh"

	"fuchsia.googlesource.com/host_target_testing/artifacts"
	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/sl4f"
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

	upToDate, err := isDeviceUpToDate(ctx, device, *rpcClient, expectedSystemImageMerkle)
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

	if build != nil {
		if err := paveDevice(ctx, device, build); err != nil {
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

	if err := validateDevice(ctx, device, rpcClient, repo, expectedSystemImageMerkle, expectedConfig, true); err != nil {
		rpcClient.Close()
		return nil, fmt.Errorf("failed to validate during initialization: %w", err)
	}

	log.Printf("initialization successful in %s", time.Now().Sub(startTime))

	return rpcClient, nil
}

func paveDevice(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
) error {
	log.Printf("Starting to pave device")
	startTime := time.Now()

	paver, err := build.GetPaver(ctx)
	if err != nil {
		return fmt.Errorf("error getting downgrade paver: %s", err)
	}

	// Reboot the device into recovery and pave it.
	if err := rebootToRecovery(ctx, device, build); err != nil {
		return fmt.Errorf("failed to reboot to recovery: %s", err)
	}

	if err = paver.Pave(ctx, c.deviceConfig.DeviceName); err != nil {
		return fmt.Errorf("device failed to pave: %s", err)
	}

	// Reconnect to the device.
	if err = device.Reconnect(ctx); err != nil {
		return fmt.Errorf("device failed to connect: %s", err)
	}

	log.Printf("paving successful in %s", time.Now().Sub(startTime))

	return nil
}

func rebootToRecovery(ctx context.Context, device *device.Client, build artifacts.Build) error {
	if !c.otaToRecovery {
		return device.RebootToRecovery(ctx)
	}

	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return fmt.Errorf("failed to get repository: %w", err)
	}

	if err := device.DownloadOTA(ctx, repo, "fuchsia-pkg://fuchsia.com/update-to-zedboot/0"); err != nil {
		return fmt.Errorf("failed to download OTA: %s", err)
	}

	return device.ExpectDisconnect(ctx, func() error {
		cmd := []string{"dm", "reboot"}

		if err := device.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			// If the device rebooted before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			if _, ok := err.(*ssh.ExitMissingError); !ok {
				return fmt.Errorf("failed to reboot: %s", err)
			}
		}

		return nil
	})
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

	expectedConfig, err := determineTargetConfig(ctx, *rpcClient)
	if err != nil {
		return fmt.Errorf("error determining target config: %s", err)
	}

	upToDate, err := isDeviceUpToDate(ctx, device, *rpcClient, expectedSystemImageMerkle)
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
	if err := validateDevice(ctx, device, *rpcClient, repo, expectedSystemImageMerkle, expectedConfig, checkABR); err != nil {
		return fmt.Errorf("failed to validate after OTA: %s", err)
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
	expectedConfig, err := determineTargetConfig(ctx, *rpcClient)
	if err != nil {
		return fmt.Errorf("error determining target config: %s", err)
	}

	upToDate, err := isDeviceUpToDate(ctx, device, *rpcClient, expectedSystemImageMerkle)
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
	if err := validateDevice(ctx, device, *rpcClient, repo, expectedSystemImageMerkle, expectedConfig, checkABR); err != nil {
		return fmt.Errorf("failed to validate after OTA: %s", err)
	}
	return nil
}

func isDeviceUpToDate(ctx context.Context, device *device.Client, rpcClient *sl4f.Client, expectedSystemImageMerkle string) (bool, error) {
	// Get the device's current /system/meta. Error out if it is the same
	// version we are about to OTA to.
	var remoteSystemImageMerkle string
	var err error
	if rpcClient == nil {
		remoteSystemImageMerkle, err = device.GetSystemImageMerkle(ctx)
	} else {
		remoteSystemImageMerkle, err = rpcClient.GetSystemImageMerkle(ctx)
	}
	if err != nil {
		return false, err
	}
	log.Printf("current system image merkle: %q", remoteSystemImageMerkle)
	log.Printf("upgrading to system image merkle: %q", expectedSystemImageMerkle)

	return expectedSystemImageMerkle == remoteSystemImageMerkle, nil
}

func determineActiveConfig(ctx context.Context, rpcClient *sl4f.Client) (*sl4f.Configuration, error) {
	if rpcClient == nil {
		log.Printf("sl4f not running, cannot determine current active partition")
		return nil, nil
	}

	activeConfig, err := rpcClient.PaverQueryActiveConfiguration(ctx)
	if err == sl4f.ErrNotSupported {
		log.Printf("device does not support querying the active configuration")
		return nil, nil
	} else if err != nil {
		return nil, err
	}

	log.Printf("device booted to slot %s", activeConfig)

	return &activeConfig, nil
}

func determineTargetConfig(ctx context.Context, rpcClient *sl4f.Client) (*sl4f.Configuration, error) {
	activeConfig, err := determineActiveConfig(ctx, rpcClient)
	if err != nil {
		return nil, fmt.Errorf("could not determine target config when querying active config: %w", err)
	}

	if activeConfig == nil {
		return nil, nil
	}

	var targetConfig sl4f.Configuration
	if *activeConfig == sl4f.ConfigurationA {
		targetConfig = sl4f.ConfigurationB
	} else {
		targetConfig = sl4f.ConfigurationA
	}

	return &targetConfig, nil
}

func validateDevice(
	ctx context.Context,
	device *device.Client,
	rpcClient *sl4f.Client,
	repo *packages.Repository,
	expectedSystemImageMerkle string,
	expectedConfig *sl4f.Configuration,
	checkABR bool,
) error {
	// At the this point the system should have been updated to the target
	// system version. Confirm the update by fetching the device's current
	// /system/meta, and making sure it is the correct version.
	upToDate, err := isDeviceUpToDate(ctx, device, rpcClient, expectedSystemImageMerkle)
	if err != nil {
		return fmt.Errorf("failed to check if device is up to date: %s", err)
	}
	if !upToDate {
		return fmt.Errorf("system version failed to update to %q", expectedSystemImageMerkle)
	}

	// Make sure the device doesn't have any broken static packages.
	// FIXME(40913): every builder should at least build sl4f as a universe package.
	if rpcClient == nil {
		if err := device.ValidateStaticPackages(ctx); err != nil {
			return fmt.Errorf("failed to validate static packages: %s", err)
		}
	} else {
		if err := rpcClient.ValidateStaticPackages(ctx); err != nil {
			return fmt.Errorf("failed to validate static packages: %s", err)
		}

		// Ensure the device is booting from the expected boot slot
		activeConfig, err := rpcClient.PaverQueryActiveConfiguration(ctx)
		if err == sl4f.ErrNotSupported {
			log.Printf("device does not support querying the active configuration")
		} else if err != nil {
			return fmt.Errorf("unable to determine active boot configuration: %s", err)
		}

		log.Printf("device booted to slot %s", activeConfig)

		if expectedConfig != nil && activeConfig != *expectedConfig {
			// FIXME(43336): during the rollout of ABR, the N-1 build might
			// not be writing to the inactive partition, so don't
			// err out during that phase. This will be removed once
			// ABR has rolled through GI.
			if checkABR {
				log.Printf("expected device to boot from slot %s, got %s (ignoring during ABR rollout)", *expectedConfig, activeConfig)
			} else {
				return fmt.Errorf("expected device to boot from slot %s, got %s", *expectedConfig, activeConfig)
			}
		}
	}

	return nil
}
