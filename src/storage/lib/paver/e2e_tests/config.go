// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pavesystemtest

import (
	"flag"
	"os"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/cli"
)

type config struct {
	archiveConfig        *cli.ArchiveConfig
	installerConfig      *cli.InstallerConfig
	deviceConfig         *cli.DeviceConfig
	downgradeBuildConfig *cli.BuildConfig
	upgradeBuildConfig   *cli.BuildConfig
}

func newConfig(fs *flag.FlagSet) (*config, error) {
	installerConfig, err := cli.NewInstallerConfig(fs)
	if err != nil {
		return nil, err
	}

	archiveConfig := cli.NewArchiveConfig(fs)
	deviceConfig := cli.NewDeviceConfig(fs)

	c := &config{
		archiveConfig:        archiveConfig,
		installerConfig:      installerConfig,
		deviceConfig:         deviceConfig,
		downgradeBuildConfig: cli.NewBuildConfigWithPrefix(fs, archiveConfig, deviceConfig, "", "downgrade-"),
		upgradeBuildConfig:   cli.NewBuildConfigWithPrefix(fs, archiveConfig, deviceConfig, os.Getenv("BUILDBUCKET_ID"), "upgrade-"),
	}

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
