// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pavesystemtest

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/errutil"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/paver"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

var c *config

func TestMain(m *testing.M) {
	log.SetPrefix("pave-test: ")
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

func TestPaveNewWithOldZedboot(t *testing.T) {
	ctx := context.Background()
	l := logger.NewLogger(
		logger.TraceLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"pave-test: ")
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
		device.NewEstimatedMonotonicTime(deviceClient, "pave-test: "),
	)
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	downgradeBuild, err := c.downgradeBuildConfig.GetBuild(ctx, deviceClient, outputDir)
	if err != nil {
		return fmt.Errorf("failed to get downgrade build: %w", err)
	}

	upgradeBuild, err := c.upgradeBuildConfig.GetBuild(ctx, deviceClient, outputDir)
	if err != nil {
		return fmt.Errorf("failed to get upgrade build: %w", err)
	}

	p, err := downgradeBuild.GetPaver(ctx)
	if err != nil {
		return fmt.Errorf("failed to get paver to pave device: %w", err)
	}

	if err := deviceClient.RebootToRecovery(ctx); err != nil {
		return fmt.Errorf("failed to reboot to recovery during paving: %w", err)
	}

	// Actually pave the device.
	if err = p.PaveWithOptions(ctx, deviceClient.Name(), paver.Options{Mode: paver.ZedbootOnly}); err != nil {
		return fmt.Errorf("device failed to pave zedboot: %w", err)
	}

	logger.Infof(ctx, "paved zedboot")

	p, err = upgradeBuild.GetPaver(ctx)
	if err != nil {
		return fmt.Errorf("failed to get paver to pave device: %w", err)
	}

	if err = p.PaveWithOptions(ctx, deviceClient.Name(), paver.Options{Mode: paver.SkipZedboot}); err != nil {
		return fmt.Errorf("device failed to pave main: %w", err)
	}

	logger.Infof(ctx, "paver completed, waiting for device to boot")

	// Reconnect to the device.
	if err = deviceClient.Reconnect(ctx); err != nil {
		return fmt.Errorf("device failed to connect after pave: %w", err)
	}

	logger.Infof(ctx, "device booted")

	return nil
}
