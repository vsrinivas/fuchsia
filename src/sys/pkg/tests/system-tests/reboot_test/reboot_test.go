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

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/artifacts"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/sl4f"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/check"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/pave"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/script"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
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
	l := logger.NewLogger(
		logger.TraceLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"reboot-test: ")
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	if err := doTest(ctx); err != nil {
		logger.Errorf(ctx, "test failed: %v", err)
		// FIXME(fxbug.dev/53362) It appears dumping a all process
		// backtraces may be causing a SIGPIPE. Disabling for now to
		// see if it will stop this test from flaking out.
		/*
			if e := errutil.HandleError(ctx, c.deviceConfig.SerialSocketPath, err); e != nil {
				logger.Errorf(ctx, "failed to dump process back traces: %v", e)
			}
		*/
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
		device.NewEstimatedMonotonicTime(deviceClient, "reboot-test: "),
	)
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	build, err := c.getBuild(ctx, outputDir)
	if err != nil {
		return fmt.Errorf("failed to get downgrade build: %w", err)
	}

	ch := make(chan *sl4f.Client, 1)
	if err := util.RunWithTimeout(ctx, c.paveTimeout, func() error {
		rpcClient, err := initializeDevice(ctx, deviceClient, build)
		ch <- rpcClient
		return err
	}); err != nil {
		return fmt.Errorf("initialization failed: %w", err)
	}

	rpcClient := <-ch
	defer func() {
		if rpcClient != nil {
			rpcClient.Close()
		}
	}()

	return testReboot(ctx, deviceClient, build, &rpcClient)
}

func testReboot(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
	rpcClient **sl4f.Client,
) error {
	for i := 1; i <= c.cycleCount; i++ {
		logger.Infof(ctx, "Reboot Attempt %d", i)

		// Protect against the test stalling out by wrapping it in a closure,
		// setting a timeout on the context, and running the actual test in a
		// closure.
		if err := util.RunWithTimeout(ctx, c.cycleTimeout, func() error {
			return doTestReboot(ctx, device, build, rpcClient)
		}); err != nil {
			return fmt.Errorf("Reboot Cycle %d failed: %w", i, err)
		}
	}

	return nil
}

func doTestReboot(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
	rpcClient **sl4f.Client,
) error {
	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return fmt.Errorf("unable to get repository: %w", err)
	}

	// Install version N on the device if it is not already on that version.
	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return fmt.Errorf("error extracting expected system image merkle: %w", err)
	}

	expectedConfig, err := check.DetermineCurrentABRConfig(ctx, *rpcClient)
	if err != nil {
		return fmt.Errorf("error determining target config: %w", err)
	}

	if err := check.ValidateDevice(
		ctx,
		device,
		*rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		false,
	); err != nil {
		return err
	}

	if err := device.Reboot(ctx); err != nil {
		return fmt.Errorf("error rebooting: %w", err)
	}

	// Disconnect from sl4f since we rebooted the device.
	//
	// FIXME(47145) To avoid fxbug.dev/47145, we need to delay
	// disconnecting from sl4f until after we reboot the device. Otherwise
	// we risk leaving the ssh session in a bad state.
	(*rpcClient).Close()

	*rpcClient, err = device.StartRpcSession(ctx, repo)
	if err != nil {
		return fmt.Errorf("unable to connect to sl4f: %w", err)
	}

	if err := check.ValidateDevice(
		ctx,
		device,
		*rpcClient,
		expectedSystemImageMerkle,
		expectedConfig,
		false,
	); err != nil {
		return fmt.Errorf("failed to validate device: %w", err)
	}

	if err := script.RunScript(ctx, device, repo, rpcClient, c.afterTestScript); err != nil {
		return fmt.Errorf("failed to run after-test-script: %w", err)
	}

	return nil
}

func initializeDevice(
	ctx context.Context,
	device *device.Client,
	build artifacts.Build,
) (*sl4f.Client, error) {
	logger.Infof(ctx, "Initializing device")

	repo, err := build.GetPackageRepository(ctx)
	if err != nil {
		return nil, err
	}

	if err := script.RunScript(ctx, device, repo, nil, c.beforeInitScript); err != nil {
		return nil, fmt.Errorf("failed to run before-init-script: %w", err)
	}

	expectedSystemImageMerkle, err := repo.LookupUpdateSystemImageMerkle()
	if err != nil {
		return nil, fmt.Errorf("error extracting expected system image merkle: %w", err)
	}

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

	rpcClient, err := device.StartRpcSession(ctx, repo)
	if err != nil {
		return nil, fmt.Errorf("unable to connect to sl4f: %w", err)
	}

	// Check if we support ABR. If so, we always boot into A after a pave.
	expectedConfig, err := check.DetermineCurrentABRConfig(ctx, rpcClient)
	if err != nil {
		rpcClient.Close()
		return nil, err
	}
	if !upToDate && expectedConfig != nil {
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
		rpcClient.Close()
		return nil, err
	}

	if err := script.RunScript(ctx, device, repo, &rpcClient, c.afterInitScript); err != nil {
		rpcClient.Close()
		return nil, fmt.Errorf("failed to run after-init-script: %w", err)
	}

	return rpcClient, nil
}
