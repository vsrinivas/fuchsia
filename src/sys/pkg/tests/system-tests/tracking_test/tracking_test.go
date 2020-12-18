// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tracking

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/sl4f"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/updater"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/check"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/pave"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

var c *config

func TestMain(m *testing.M) {
	log.SetPrefix("tracking-test: ")
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
	l := logger.NewLogger(
		logger.TraceLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"tracking-test: ")
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	deviceClient, err := c.deviceConfig.NewDeviceClient(ctx)
	if err != nil {
		t.Fatalf("failed to create ota test client: %s", err)
	}
	defer deviceClient.Close()

	l = logger.NewLogger(
		logger.TraceLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		device.NewEstimatedMonotonicTime(deviceClient, "tracking-test: "),
	)
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	// Creating a sl4f.Client requires knowing the build currently running
	// on the device, which not all test cases know during start. Store the
	// one true client here, and pass around pointers to the various
	// functions that may use it or device.Client to interact with the
	// target. All OTA attempts must first Close and nil out an existing
	// rpcClient and replace it with a new one after reboot. The final
	// rpcClient, if present, will be closed by the defer here.
	var rpcClient *sl4f.Client
	defer func() {
		if rpcClient != nil {
			rpcClient.Close()
		}
	}()

	if c.shouldRepaveDevice() {
		ch := make(chan *sl4f.Client, 1)
		if err := util.RunWithTimeout(ctx, c.paveTimeout, func() error {
			rpcClient, err = paveDevice(ctx, deviceClient)
			ch <- rpcClient
			return err
		}); err != nil {
			t.Fatalf("Failed to initialize device: %v", err)
		}
		rpcClient = <-ch
	}

	testTrackingOTAs(t, ctx, deviceClient, &rpcClient)
}

func testTrackingOTAs(t *testing.T, ctx context.Context, device *device.Client, rpcClient **sl4f.Client) {
	builder, err := c.getUpgradeBuilder()
	if err != nil {
		t.Fatal(err)
	}

	// We only check ABR after the first update, since we can't be sure if
	// the initial version of Fuchsia is recent enough to support ABR, but
	// it should support ABR after the first OTA.
	checkABR := false

	lastBuildID := ""
	attempt := 1
	for {
		logger.Infof(ctx, "Look up latest build for builder %s", builder)

		buildID, err := builder.GetLatestBuildID(ctx)
		if err != nil {
			t.Fatalf("error getting latest build for builder %s: %s", builder, err)
		}

		if buildID == lastBuildID {
			logger.Infof(ctx, "already updated to %s, sleeping", buildID)
			time.Sleep(60 * time.Second)
			continue
		}
		logger.Infof(ctx, "Tracking Test Attempt %d upgrading from build %s to build %s", attempt, lastBuildID, buildID)

		if err := util.RunWithTimeout(ctx, c.cycleTimeout, func() error {
			return testTrackingOTAAttempt(ctx, device, rpcClient, buildID, checkABR)
		}); err != nil {
			t.Fatalf("Tracking Test Attempt %d failed: %s", attempt, err)
		}

		logger.Infof(ctx, "Tracking Test Attempt %d successful", attempt)
		logger.Infof(ctx, "------------------------------------------------------------------------------")

		checkABR = true
		lastBuildID = buildID
		attempt += 1
	}
}

func testTrackingOTAAttempt(
	ctx context.Context,
	device *device.Client,
	rpcClient **sl4f.Client,
	buildID string,
	checkABR bool,
) error {
	outputDir, cleanup, err := c.archiveConfig.OutputDir()
	if err != nil {
		return fmt.Errorf("failed to get output directory: %w", err)
	}
	defer cleanup()

	build, err := c.archiveConfig.BuildArchive().GetBuildByID(ctx, buildID, outputDir, nil)
	if err != nil {
		return fmt.Errorf("failed to find build %s: %w", buildID, err)
	}

	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return fmt.Errorf("failed to get repo for build: %w", err)
	}

	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("failed to get repo system image merkle: %w", err)
	}

	upToDate, err := check.IsDeviceUpToDate(ctx, device, expectedSystemImageMerkle)
	if err != nil {
		return fmt.Errorf("failed to check if device is up to date: %w", err)
	}
	if upToDate {
		logger.Infof(ctx, "device already up to date")
		return nil
	}

	logger.Infof(ctx, "\n\n")
	logger.Infof(ctx, "OTAing to %s", build)

	return systemOTA(ctx, device, rpcClient, repo, checkABR)
}

func paveDevice(ctx context.Context, device *device.Client) (*sl4f.Client, error) {
	outputDir, cleanup, err := c.archiveConfig.OutputDir()
	if err != nil {
		return nil, err
	}
	defer cleanup()

	downgradeBuild, err := c.getDowngradeBuild(ctx, outputDir)
	if err != nil {
		return nil, fmt.Errorf("failed to get downgrade build: %w", err)
	}

	downgradeRepo, err := downgradeBuild.GetPackageRepository(ctx)
	if err != nil {
		return nil, fmt.Errorf("error getting downgrade repository: %w", err)
	}

	logger.Infof(ctx, "starting pave")

	expectedSystemImageMerkle, err := downgradeRepo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return nil, fmt.Errorf("error extracting expected system image merkle: %w", err)
	}

	if err := pave.PaveDevice(ctx, device, downgradeBuild); err != nil {
		return nil, fmt.Errorf("failed to pave device during initialization: %w", err)
	}

	rpcClient, err := device.StartRpcSession(ctx, downgradeRepo)
	if err != nil {
		// FIXME(40913): every downgrade builder should at least build
		// sl4f as a universe package.
		logger.Infof(ctx, "unable to connect to sl4f after pave: %s", err)
		//t.Fatalf("unable to connect to sl4f after pave: %s", err)
	}

	// We always boot into the A partition after a pave.
	expectedConfig := sl4f.ConfigurationA

	if err := check.ValidateDevice(
		ctx,
		device,
		rpcClient,
		expectedSystemImageMerkle,
		&expectedConfig,
		true,
	); err != nil {
		return nil, err
	}

	logger.Infof(ctx, "paving successful")

	return rpcClient, nil
}

func systemOTA(ctx context.Context, device *device.Client, rpcClient **sl4f.Client, repo *packages.Repository, checkABR bool) error {
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %w", err)
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
		return fmt.Errorf("error determining target config: %w", err)
	}

	upToDate, err := check.IsDeviceUpToDate(ctx, device, expectedSystemImageMerkle)
	if err != nil {
		return fmt.Errorf("failed to check if device is up to date: %w", err)
	}
	if upToDate {
		return fmt.Errorf("device already updated to the expected version %q", expectedSystemImageMerkle)
	}

	u := updater.NewSystemUpdater(repo, updatePackageUrl)
	if err := u.Update(ctx, device); err != nil {
		return fmt.Errorf("failed to download OTA: %w", err)
	}

	logger.Infof(ctx, "Rebooting device")
	startTime := time.Now()

	if err = device.Reboot(ctx); err != nil {
		return fmt.Errorf("device failed to reboot after OTA applied: %w", err)
	}

	logger.Infof(ctx, "Reboot complete in %s", time.Now().Sub(startTime))
	logger.Infof(ctx, "Validating device")

	// Disconnect from sl4f since we rebooted the device.
	//
	// FIXME(47145) To avoid fxbug.dev/47145, we need to delay
	// disconnecting from sl4f until after we reboot the device. Otherwise
	// we risk leaving the ssh session in a bad state.
	if *rpcClient != nil {
		(*rpcClient).Close()
		*rpcClient = nil
	}

	*rpcClient, err = device.StartRpcSession(ctx, repo)
	if err != nil {
		return fmt.Errorf("unable to connect to sl4f after OTA: %w", err)
	}

	if err := check.ValidateDevice(
		ctx,
		device,
		*rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		checkABR,
	); err != nil {
		return fmt.Errorf("failed to validate after OTA: %w", err)
	}
	return nil
}
