// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package reboot

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"testing"

	"fuchsia.googlesource.com/host_target_testing/artifacts"
	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/sl4f"
	"fuchsia.googlesource.com/system_tests/check"
	"fuchsia.googlesource.com/system_tests/pave"
	"fuchsia.googlesource.com/system_tests/script"
)

var c *config

func TestMain(m *testing.M) {
	log.SetPrefix("reboot-test: ")
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

func TestReboot(t *testing.T) {
	ctx := context.Background()

	outputDir, cleanup, err := c.archiveConfig.OutputDir()
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()

	device, err := c.deviceConfig.NewDeviceClient(ctx)
	if err != nil {
		t.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	build, err := c.getBuild(ctx, outputDir)
	if err != nil {
		log.Fatalf("failed to get downgrade build: %v", err)
	}

	if err := initializeDevice(ctx, device, build); err != nil {
		t.Fatalf("paving failed: %s", err)
	}

	testReboot(ctx, device, build)
}

func testReboot(ctx context.Context, device *device.Client, build artifacts.Build) {
	for i := 1; i <= c.cycleCount; i++ {
		log.Printf("Reboot Attempt %d", i)

		if err := doTestReboot(ctx, device, build); err != nil {
			log.Fatalf("OTA Cycle %d timed out: %s", i, err)
		}
	}
}

func doTestReboot(ctx context.Context, device *device.Client, build artifacts.Build) error {
	ctx, cancel := context.WithTimeout(ctx, c.cycleTimeout)
	defer cancel()

	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return fmt.Errorf("unable to get repository: %w", err)
	}

	rpcClient, err := device.StartRpcSession(ctx, repo)
	if err != nil {
		return fmt.Errorf("unable to connect to sl4f: %s", err)
	}
	defer func() {
		if rpcClient != nil {
			rpcClient.Close()
		}
	}()

	// Install version N on the device if it is not already on that version.
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %s", err)
	}

	expectedConfig, err := check.DetermineActiveABRConfig(ctx, rpcClient)
	if err != nil {
		return fmt.Errorf("error determining target config: %s", err)
	}

	if err := check.ValidateDevice(
		ctx,
		device,
		rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		false,
	); err != nil {
		return err
	}

	// Disconnect from sl4f since the OTA should reboot the device.
	rpcClient.Close()
	rpcClient = nil

	if err := device.Reboot(ctx); err != nil {
		return fmt.Errorf("error rebooting: %s", err)
	}

	rpcClient, err = device.StartRpcSession(ctx, repo)
	if err != nil {
		return fmt.Errorf("unable to connect to sl4f: %s", err)
	}

	if err := check.ValidateDevice(
		ctx,
		device,
		rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		false,
	); err != nil {
		return fmt.Errorf("failed to validate device: %s", err)
	}

	if err := script.RunScript(ctx, device, repo, &rpcClient, c.afterTestScript); err != nil {
		return fmt.Errorf("failed to run after-test-script: %w", err)
	}

	return nil
}

func initializeDevice(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
) error {
	log.Printf("Initializing device")

	ctx, cancel := context.WithTimeout(ctx, c.paveTimeout)
	defer cancel()

	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return err
	}

	if err := script.RunScript(ctx, device, repo, nil, c.beforeInitScript); err != nil {
		return fmt.Errorf("failed to run before-init-script: %w", err)
	}

	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %w", err)
	}

	// Only pave if the device is not running the expected version.
	upToDate, err := check.IsDeviceUpToDate(ctx, device, expectedSystemImageMerkle)
	if err != nil {
		return fmt.Errorf("failed to check if up to date during initialization: %w", err)
	}
	if upToDate {
		log.Printf("device already up to date")
		return nil
	}

	if err := pave.PaveDevice(ctx, device, build, c.otaToRecovery); err != nil {
		return fmt.Errorf("failed to pave device during initialization: %w", err)
	}

	rpcClient, err := device.StartRpcSession(ctx, repo)
	if err != nil {
		return fmt.Errorf("unable to connect to sl4f: %w", err)
	}
	defer rpcClient.Close()

	// Check if we support ABR. If so, we always boot into A after a pave.
	expectedConfig, err := check.DetermineActiveABRConfig(ctx, rpcClient)
	if err != nil {
		return err
	}
	if expectedConfig != nil {
		config := sl4f.ConfigurationA
		expectedConfig = &config
	}

	if err := check.ValidateDevice(
		ctx,
		device,
		rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		false,
	); err != nil {
		return err
	}

	if err := script.RunScript(ctx, device, repo, &rpcClient, c.afterInitScript); err != nil {
		return fmt.Errorf("failed to run after-init-script: %w", err)
	}

	return nil
}
