// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package reboot

import (
	"context"
	"flag"
	"log"
	"os"
	"testing"
	"time"

	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/sl4f"
)

var c *config

func TestMain(m *testing.M) {
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
	log.SetPrefix("reboot-test: ")

	ctx := context.Background()

	outputDir, cleanup, err := c.archiveConfig.OutputDir()
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()

	device, err := c.deviceConfig.NewDeviceClient()
	if err != nil {
		t.Fatalf("failed to create ota test client: %s", err)
	}
	defer device.Close()

	paveDevice(t, ctx, device, outputDir)
	testReboot(t, ctx, device, outputDir)
}

func testReboot(t *testing.T, ctx context.Context, device *device.Client, outputDir string) {
	for i := 0; i < c.cycleCount; i++ {
		log.Printf("Reboot Attempt %d", i+1)

		err := withTimeout(ctx, c.cycleTimeout, func() {
			doTestReboot(t, ctx, device, outputDir)
		})
		if err != nil {
			t.Fatalf("OTA Cycle timed out: %s", err)
		}
	}
}

func doTestReboot(t *testing.T, ctx context.Context, device *device.Client, outputDir string) {
	repo, err := c.getRepository(outputDir)
	if err != nil {
		t.Fatal(err)
	}

	rpcClient, err := device.StartRpcSession(ctx, repo)
	if err != nil {
		t.Fatalf("unable to connect to sl4f: %s", err)
	}
	defer rpcClient.Close()

	// Install version N on the device if it is not already on that version.
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		t.Fatalf("error extracting expected system image merkle: %s", err)
	}

	expectedConfig, err := determineActiveConfig(ctx, rpcClient)
	if err != nil {
		t.Fatalf("error determining target config: %s", err)
	}

	validateDevice(t, ctx, device, rpcClient, expectedSystemImageMerkle, expectedConfig)

	if err := device.Reboot(ctx, repo, &rpcClient); err != nil {
		t.Fatalf("error rebooting: %s", err)
	}

	validateDevice(t, ctx, device, rpcClient, expectedSystemImageMerkle, expectedConfig)
}

func paveDevice(t *testing.T, ctx context.Context, device *device.Client, outputDir string) {
	err := withTimeout(ctx, c.paveTimeout, func() {
		doPaveDevice(t, ctx, device, outputDir)
	})
	if err != nil {
		t.Fatalf("Paving timed out: %s", err)
	}
}

func doPaveDevice(t *testing.T, ctx context.Context, device *device.Client, outputDir string) {
	if !c.shouldRepaveDevice() {
		return
	}

	repo, err := c.getRepository(outputDir)
	if err != nil {
		t.Fatal(err)
	}

	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		t.Fatalf("error extracting expected system image merkle: %s", err)
	}

	// Only pave if the device is not running the expected version.
	if isDeviceUpToDate(t, ctx, device, expectedSystemImageMerkle) {
		log.Printf("device already up to date")
		return
	}

	paver, err := c.getPaver(outputDir)
	if err != nil {
		t.Fatal(err)
	}

	log.Printf("starting pave")

	// Reboot the device into recovery and pave it.
	if err = device.RebootToRecovery(); err != nil {
		t.Fatalf("failed to reboot to recovery: %s", err)
	}

	if err = paver.Pave(c.deviceConfig.DeviceName); err != nil {
		t.Fatalf("device failed to pave: %s", err)
	}

	// Wait for the device to come online.
	device.WaitForDeviceToBeConnected()

	rpcClient, err := device.StartRpcSession(ctx, repo)
	if err != nil {
		t.Fatalf("unable to connect to sl4f: %s", err)
	}
	defer rpcClient.Close()

	// Check if we support ABR. If so, we always boot into A after a pave.
	expectedConfig, err := determineActiveConfig(ctx, rpcClient)
	if err != nil {
		t.Fatal(err)
	}
	if expectedConfig != nil {
		config := sl4f.ConfigurationA
		expectedConfig = &config
	}

	validateDevice(t, ctx, device, rpcClient, expectedSystemImageMerkle, expectedConfig)

	log.Printf("paving successful")
}

func isDeviceUpToDate(t *testing.T, ctx context.Context, device *device.Client, expectedSystemImageMerkle string) bool {
	// Get the device's current /system/meta. Error out if it is the same
	// version we are about to OTA to.
	remoteSystemImageMerkle, err := device.GetSystemImageMerkle()
	if err != nil {
		t.Fatal(err)
	}
	log.Printf("current system image merkle: %q", remoteSystemImageMerkle)
	log.Printf("upgrading to system image merkle: %q", expectedSystemImageMerkle)

	return expectedSystemImageMerkle == remoteSystemImageMerkle
}

func determineActiveConfig(ctx context.Context, rpcClient *sl4f.Client) (*sl4f.Configuration, error) {
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

func validateDevice(
	t *testing.T,
	ctx context.Context,
	device *device.Client,
	rpcClient *sl4f.Client,
	expectedSystemImageMerkle string,
	expectedConfig *sl4f.Configuration,
) {
	// At the this point the system should have been updated to the target
	// system version. Confirm the update by fetching the device's current
	// /system/meta, and making sure it is the correct version.
	if !isDeviceUpToDate(t, ctx, device, expectedSystemImageMerkle) {
		t.Fatalf("system version failed to update to %q", expectedSystemImageMerkle)
	}

	// Make sure the device doesn't have any broken static packages.
	if err := rpcClient.ValidateStaticPackages(ctx); err != nil {
		t.Fatal(err)
	}

	// Ensure the device is booting from the expected boot slot
	activeConfig, err := determineActiveConfig(ctx, rpcClient)
	if err != nil {
		t.Fatalf("unable to determine active boot configuration: %s", err)
	}

	if expectedConfig != nil && activeConfig != nil && *activeConfig != *expectedConfig {
		t.Fatalf("expected device to boot from slot %s, got %s", *expectedConfig, *activeConfig)
	}
}

func withTimeout(ctx context.Context, timeout time.Duration, f func()) error {
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	ch := make(chan struct{})
	go func() {
		f()
		ch <- struct{}{}
	}()

	select {
	case <-ch:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}
