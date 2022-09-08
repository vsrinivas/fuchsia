// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tracking

import (
	"flag"
	"fmt"
	"os"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/cli"
)

type config struct {
	archiveConfig        *cli.ArchiveConfig
	installerConfig      *cli.InstallerConfig
	deviceConfig         *cli.DeviceConfig
	downgradeBuildConfig *cli.BuildConfig
	upgradeBuildConfig   *cli.BuildConfig
	paveTimeout          time.Duration
	cycleCount           int
	cycleTimeout         time.Duration
}

func newConfig(fs *flag.FlagSet) (*config, error) {
	installerConfig, err := cli.NewInstallerConfig(fs)
	if err != nil {
		return nil, err
	}

	archiveConfig := cli.NewArchiveConfig(fs)
	deviceConfig := cli.NewDeviceConfig(fs)

	c := &config{
		archiveConfig:        cli.NewArchiveConfig(fs),
		deviceConfig:         cli.NewDeviceConfig(fs),
		installerConfig:      installerConfig,
		downgradeBuildConfig: cli.NewBuildConfigWithPrefix(fs, archiveConfig, deviceConfig, "", "downgrade-"),
		upgradeBuildConfig:   cli.NewBuildConfigWithPrefix(fs, archiveConfig, deviceConfig, os.Getenv("BUILDBUCKET_ID"), "upgrade-"),
	}

	fs.DurationVar(&c.paveTimeout, "pave-timeout", 1<<63-1, "Err if a pave takes longer than this time (default is no timeout)")
	fs.IntVar(&c.cycleCount, "cycle-count", 1, "How many cycles to run the test before completing (default is 1)")
	fs.DurationVar(&c.cycleTimeout, "cycle-timeout", 1<<63-1, "Err if a test cycle takes longer than this time (default is no timeout)")

	return c, nil
}

func (c *config) validate() error {
	if err := c.downgradeBuildConfig.Validate(); err != nil {
		return err
	}

	if err := c.upgradeBuildConfig.Validate(); err != nil {
		return err
	}

	if c.cycleCount < 1 {
		return fmt.Errorf("-cycle-count must be >= 1")
	}

	return nil
}
