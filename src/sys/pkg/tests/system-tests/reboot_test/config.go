// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package reboot

import (
	"context"
	"flag"
	"fmt"
	"os"
	"time"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/artifacts"

	systemTestConfig "go.fuchsia.dev/fuchsia/src/sys/pkg/tests/system-tests/config"
)

type config struct {
	archiveConfig    *systemTestConfig.ArchiveConfig
	installerConfig  *systemTestConfig.InstallerConfig
	deviceConfig     *systemTestConfig.DeviceConfig
	packagesPath     string
	builderName      string
	buildID          string
	fuchsiaBuildDir  string
	paveTimeout      time.Duration
	cycleCount       int
	cycleTimeout     time.Duration
	beforeInitScript string
	afterInitScript  string
	afterTestScript  string
}

func newConfig(fs *flag.FlagSet) (*config, error) {
	installerConfig, err := systemTestConfig.NewInstallerConfig(fs)
	if err != nil {
		return nil, err
	}

	c := &config{
		archiveConfig:   systemTestConfig.NewArchiveConfig(fs),
		installerConfig: installerConfig,
		deviceConfig:    systemTestConfig.NewDeviceConfig(fs),
	}

	fs.StringVar(&c.builderName, "builder-name", "", "Pave to the latest version of this builder")
	fs.StringVar(&c.buildID, "build-id", os.Getenv("BUILDBUCKET_ID"), "Pave to this specific build id")
	fs.StringVar(&c.fuchsiaBuildDir, "fuchsia-build-dir", "", "Pave to the build in this directory")
	fs.IntVar(&c.cycleCount, "cycle-count", 1, "How many cycles to run the test before completing (default is 1)")
	fs.DurationVar(&c.paveTimeout, "pave-timeout", 5*time.Minute, "Err if a pave takes longer than this time (default 5 minutes)")
	fs.DurationVar(&c.cycleTimeout, "cycle-timeout", 5*time.Minute, "Err if a test cycle takes longer than this time (default is 5 minutes)")
	fs.StringVar(&c.beforeInitScript, "before-init-script", "", "Run this script before initializing device for testing")
	fs.StringVar(&c.afterInitScript, "after-init-script", "", "Run this script after initializing device for testing")
	fs.StringVar(&c.afterTestScript, "after-test-script", "", "Run this script after a test step")

	return c, nil
}

func (c *config) validate() error {
	defined := 0
	for _, s := range []string{
		c.builderName,
		c.buildID,
		c.fuchsiaBuildDir,
	} {
		if s != "" {
			defined += 1
		}
	}
	if defined > 1 {
		return fmt.Errorf("-builder-name, -build-id, and -fuchsia-build-dir are mutually exclusive")
	}

	return nil
}

func (c *config) getBuilder() (*artifacts.Builder, error) {
	if c.builderName == "" {
		return nil, fmt.Errorf("builder not specified")
	}

	return c.archiveConfig.BuildArchive().GetBuilder(c.builderName), nil
}

func (c *config) getBuildID(ctx context.Context) (string, error) {
	if c.builderName != "" && c.buildID == "" {
		b, err := c.getBuilder()
		if err != nil {
			return "", err
		}
		id, err := b.GetLatestBuildID(ctx)
		if err != nil {
			return "", fmt.Errorf("failed to lookup build id: %w", err)
		}
		c.buildID = id
	}

	return c.buildID, nil
}

func (c *config) getBuild(ctx context.Context, dir string) (artifacts.Build, error) {
	sshPrivateKey, err := c.deviceConfig.SSHPrivateKey()
	if err != nil {
		return nil, fmt.Errorf("failed to get ssh key: %w", err)
	}

	buildID, err := c.getBuildID(ctx)
	if err != nil {
		return nil, err
	}

	if buildID != "" {
		return c.archiveConfig.BuildArchive().GetBuildByID(ctx, buildID, dir, sshPrivateKey.PublicKey())
	}

	if c.fuchsiaBuildDir != "" {
		return artifacts.NewFuchsiaDirBuild(c.fuchsiaBuildDir, sshPrivateKey.PublicKey()), nil
	}

	return nil, fmt.Errorf("build not specified")
}
