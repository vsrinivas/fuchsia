// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pavesystemtest

import (
	"flag"
	"os"
	"path/filepath"
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
	tftpBlockSize        uint64
}

func newConfig(fs *flag.FlagSet) (*config, error) {
	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests")

	installerConfig, err := cli.NewInstallerConfig(fs, testDataPath)
	if err != nil {
		return nil, err
	}

	archiveConfig := cli.NewArchiveConfig(fs, testDataPath)
	deviceConfig := cli.NewDeviceConfig(fs, testDataPath)

	c := &config{
		archiveConfig:        archiveConfig,
		installerConfig:      installerConfig,
		deviceConfig:         deviceConfig,
		downgradeBuildConfig: cli.NewBuildConfigWithPrefix(fs, archiveConfig, deviceConfig, "", "downgrade-"),
		upgradeBuildConfig:   cli.NewBuildConfigWithPrefix(fs, archiveConfig, deviceConfig, os.Getenv("BUILDBUCKET_ID"), "upgrade-"),
	}

	fs.DurationVar(&c.paveTimeout, "pave-timeout", 5*time.Minute, "Err if a pave takes longer than this time (default is 5 minutes)")
	fs.IntVar(&c.cycleCount, "cycle-count", 1, "How many cycles to run the test before completing (default is 1)")
	fs.DurationVar(&c.cycleTimeout, "cycle-timeout", 20*time.Minute, "Err if a test cycle takes longer than this time (default is 10 minutes)")
	fs.Uint64Var(&c.tftpBlockSize, "tftp-block-size", 1428, "tftp block size (default 1428)")

	return c, nil
}

func (c *config) validate() error {
	if err := c.downgradeBuildConfig.Validate(); err != nil {
		return err
	}

	if err := c.upgradeBuildConfig.Validate(); err != nil {
		return err
	}

	return nil
}
