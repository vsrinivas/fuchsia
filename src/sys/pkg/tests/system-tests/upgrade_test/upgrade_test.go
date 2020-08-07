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

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/artifacts"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/sl4f"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/check"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/errutil"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/pave"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/script"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
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
	l := logger.NewLogger(
		logger.TraceLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"upgrade-test: ")
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	if err := doTest(ctx); err != nil {
		logger.Errorf(ctx, "test failed: %v", err)
		if e := errutil.HandleError(ctx, c.deviceConfig.SerialSocketPath, err); e != nil {
			logger.Errorf(ctx, "failed to dump process back traces: %v", e)
		}
		t.Fatal(err)
	}
}

func doTest(ctx context.Context) error {
	outputDir, cleanup, err := c.archiveConfig.OutputDir()
	if err != nil {
		return fmt.Errorf("failed to get output directory: %w", err)
	}
	defer cleanup()

	deviceClient, err := c.deviceConfig.NewDeviceClient(ctx)
	if err != nil {
		return fmt.Errorf("failed to create ota test client: %w", err)
	}
	defer deviceClient.Close()

	l := logger.NewLogger(
		logger.TraceLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		device.NewEstimatedMonotonicTime(deviceClient, "upgrade-test: "),
	)
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	downgradeBuild, err := c.getDowngradeBuild(ctx, deviceClient, outputDir)
	if err != nil {
		return fmt.Errorf("failed to get downgrade build: %w", err)
	}

	upgradeBuild, err := c.getUpgradeBuild(ctx, deviceClient, outputDir)
	if err != nil {
		return fmt.Errorf("failed to get upgrade build: %w", err)
	}

	ch := make(chan *sl4f.Client, 1)
	if err := util.RunWithTimeout(ctx, c.paveTimeout, func() error {
		rpcClient, err := initializeDevice(ctx, deviceClient, downgradeBuild)
		ch <- rpcClient
		return err
	}); err != nil {
		return fmt.Errorf("device failed to initialize: %w", err)
	}

	rpcClient := <-ch
	defer func() {
		if rpcClient != nil {
			rpcClient.Close()
		}
	}()

	return testOTAs(ctx, deviceClient, upgradeBuild, &rpcClient)
}

func testOTAs(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
	rpcClient **sl4f.Client,
) error {
	for i := 1; i <= c.cycleCount; i++ {
		logger.Infof(ctx, "OTA Attempt %d", i)

		if err := util.RunWithTimeout(ctx, c.cycleTimeout, func() error {
			return doTestOTAs(ctx, device, build, rpcClient)
		}); err != nil {
			return fmt.Errorf("OTA Attempt %d failed: %w", i, err)
		}
	}

	return nil
}

func doTestOTAs(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
	rpcClient **sl4f.Client,
) error {
	logger.Infof(ctx, "Starting OTA test cycle. Time out in %s", c.cycleTimeout)

	startTime := time.Now()

	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return fmt.Errorf("error getting repository: %w", err)
	}

	// Install version N on the device if it is not already on that version.
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %w", err)
	}

	upToDate, err := check.IsDeviceUpToDate(ctx, device, expectedSystemImageMerkle)
	if err != nil {
		return fmt.Errorf("failed to check if device is up to date: %w", err)
	}
	if !upToDate {
		logger.Infof(ctx, "starting OTA from N-1 -> N test")
		otaTime := time.Now()
		if err := systemOTA(ctx, device, rpcClient, repo, true); err != nil {
			return fmt.Errorf("OTA from N-1 -> N failed: %w", err)
		}
		logger.Infof(ctx, "OTA from N-1 -> N successful in %s", time.Now().Sub(otaTime))
	}

	logger.Infof(ctx, "starting OTA N -> N' test")
	otaTime := time.Now()
	if err := systemPrimeOTA(ctx, device, rpcClient, repo, false); err != nil {
		return fmt.Errorf("OTA from N -> N' failed: %w", err)
	}
	logger.Infof(ctx, "OTA from N -> N' successful in %s", time.Now().Sub(otaTime))

	logger.Infof(ctx, "starting OTA N' -> N test")
	otaTime = time.Now()
	if err := systemOTA(ctx, device, rpcClient, repo, false); err != nil {
		return fmt.Errorf("OTA from N' -> N failed: %w", err)
	}
	logger.Infof(ctx, "OTA from N' -> N successful in %s", time.Now().Sub(otaTime))
	logger.Infof(ctx, "OTA cycle sucessful in %s", time.Now().Sub(startTime))

	return nil
}

func initializeDevice(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
) (*sl4f.Client, error) {
	logger.Infof(ctx, "Initializing device")

	startTime := time.Now()

	var repo *packages.Repository
	var expectedSystemImageMerkle string
	var err error

	if build != nil {
		repo, err = build.GetPackageRepository(ctx)
		if err != nil {
			return nil, fmt.Errorf("error getting downgrade repository: %w", err)
		}

		expectedSystemImageMerkle, err = repo.LookupUpdateSystemImageMerkle()
		if err != nil {
			return nil, fmt.Errorf("error extracting expected system image merkle: %w", err)
		}
	}

	if err := script.RunScript(ctx, device, repo, nil, c.beforeInitScript); err != nil {
		return nil, fmt.Errorf("failed to run before-init-script: %w", err)
	}

	if build != nil {
		// Only pave if the device is not running the expected version.
		upToDate, err := check.IsDeviceUpToDate(ctx, device, expectedSystemImageMerkle)
		if err != nil {
			return nil, fmt.Errorf("failed to check if up to date during initialization: %w", err)
		}
		if upToDate {
			logger.Infof(ctx, "device already up to date")
		} else {
			if err := pave.PaveDevice(ctx, device, build, c.otaToRecovery); err != nil {
				return nil, fmt.Errorf("failed to pave device during initialization: %w", err)
			}
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
		if rpcClient != nil {
			rpcClient.Close()
		}
		return nil, fmt.Errorf("failed to validate during initialization: %w", err)
	}

	if err := script.RunScript(ctx, device, repo, &rpcClient, c.afterInitScript); err != nil {
		return nil, fmt.Errorf("failed to run after-init-script: %w", err)
	}

	logger.Infof(ctx, "initialization successful in %s", time.Now().Sub(startTime))

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
		"fuchsia-pkg://fuchsia.com/update/0",
		checkABR,
	)
}

func systemPrimeOTA(ctx context.Context, device *device.Client, rpcClient **sl4f.Client, repo *packages.Repository, checkABR bool) error {
	expectedSystemImageMerkle, err := repo.LookupUpdatePrimeSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %w", err)
	}

	return otaToPackage(
		ctx,
		device,
		rpcClient,
		repo,
		expectedSystemImageMerkle,
		"fuchsia-pkg://fuchsia.com/update_prime/0",
		checkABR,
	)
}

func otaToPackage(
	ctx context.Context,
	device *device.Client,
	rpcClient **sl4f.Client,
	repo *packages.Repository,
	expectedSystemImageMerkle string,
	updatePackageURL string,
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

	u, err := c.installerConfig.Updater(repo, updatePackageURL)
	if err != nil {
		return fmt.Errorf("failed to create updater: %w", err)
	}

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

	if err := script.RunScript(ctx, device, repo, rpcClient, c.afterTestScript); err != nil {
		return fmt.Errorf("failed to run test script after OTA: %w", err)
	}

	return nil
}
