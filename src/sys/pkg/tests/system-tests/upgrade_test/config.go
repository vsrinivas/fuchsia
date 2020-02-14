// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package upgrade

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/host_target_testing/artifacts"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/paver"

	"fuchsia.googlesource.com/system_tests/config"
)

type Config struct {
	archiveConfig            *config.ArchiveConfig
	deviceConfig             *config.DeviceConfig
	downgradeBuilderName     string
	downgradeBuildID         string
	downgradeFuchsiaBuildDir string
	upgradeBuilderName       string
	upgradeBuildID           string
	upgradeFuchsiaBuildDir   string
	LongevityTest            bool
	paveTimeout              time.Duration
	cycleCount               int
	cycleTimeout             time.Duration
}

func NewConfig(fs *flag.FlagSet) (*Config, error) {
	c := &Config{
		archiveConfig: config.NewArchiveConfig(fs),
		deviceConfig:  config.NewDeviceConfig(fs),
	}

	fs.StringVar(&c.downgradeBuilderName, "downgrade-builder-name", "", "downgrade to the latest version of this builder")
	fs.StringVar(&c.downgradeBuildID, "downgrade-build-id", "", "downgrade to this specific build id")
	fs.StringVar(&c.downgradeFuchsiaBuildDir, "downgrade-fuchsia-build-dir", "", "Path to the downgrade fuchsia build dir")
	fs.StringVar(&c.upgradeBuilderName, "upgrade-builder-name", "", "upgrade to the latest version of this builder")
	fs.StringVar(&c.upgradeBuildID, "upgrade-build-id", os.Getenv("BUILDBUCKET_ID"), "upgrade to this build id (default is $BUILDBUCKET_ID)")
	fs.StringVar(&c.upgradeFuchsiaBuildDir, "upgrade-fuchsia-build-dir", "", "Path to the upgrade fuchsia build dir")
	fs.BoolVar(&c.LongevityTest, "longevity-test", false, "Continuously update to the latest repository")
	fs.DurationVar(&c.paveTimeout, "pave-timeout", 1<<63-1, "Err if a pave takes longer than this time (default is no timeout)")
	fs.IntVar(&c.cycleCount, "cycle-count", 1, "How many cycles to run the test before completing (default is 1)")
	fs.DurationVar(&c.cycleTimeout, "cycle-timeout", 1<<63-1, "Err if a test cycle takes longer than this time (default is no timeout)")

	return c, nil
}

func (c *Config) Validate() error {
	defined := 0
	for _, s := range []string{
		c.downgradeBuilderName,
		c.downgradeBuildID,
		c.downgradeFuchsiaBuildDir,
	} {
		if s != "" {
			defined += 1
		}
	}
	if defined > 1 {
		return fmt.Errorf("-downgrade-builder-name, -downgrade-build-id, and -downgrade-fuchsia-build-dir are mutually exclusive")
	}

	defined = 0
	for _, s := range []string{c.upgradeBuilderName, c.upgradeBuildID, c.upgradeFuchsiaBuildDir} {
		if s != "" {
			defined += 1
		}
	}
	if defined != 1 {
		return fmt.Errorf("exactly one of -upgrade-builder-name, -upgrade-build-id, or -upgrade-fuchsia-build-dir must be specified")
	}

	if c.LongevityTest && c.upgradeBuilderName == "" {
		return fmt.Errorf("-longevity-test requires -upgrade-builder-name to be specified")
	}

	if c.cycleCount < 1 {
		return fmt.Errorf("-cycle-count must be >= 1")
	}

	return nil
}

func (c *Config) ShouldRepaveDevice() bool {
	return c.downgradeBuildID != "" || c.downgradeBuilderName != "" || c.downgradeFuchsiaBuildDir != ""
}

func (c *Config) GetDowngradeBuilder() (*artifacts.Builder, error) {
	if c.downgradeBuilderName == "" {
		return nil, fmt.Errorf("downgrade builder not specified")
	}

	return c.archiveConfig.BuildArchive().GetBuilder(c.downgradeBuilderName), nil
}

func (c *Config) GetDowngradeBuildID() (string, error) {
	if c.downgradeBuilderName != "" && c.downgradeBuildID == "" {
		b, err := c.GetDowngradeBuilder()
		if err != nil {
			return "", err
		}
		id, err := b.GetLatestBuildID()
		if err != nil {
			return "", fmt.Errorf("failed to lookup build id: %s", err)
		}
		c.downgradeBuildID = id
	}

	return c.downgradeBuildID, nil
}

func (c *Config) GetUpgradeBuilder() (*artifacts.Builder, error) {
	if c.upgradeBuilderName == "" {
		return nil, fmt.Errorf("upgrade builder not specified")
	}

	return c.archiveConfig.BuildArchive().GetBuilder(c.upgradeBuilderName), nil
}

func (c *Config) GetUpgradeBuildID() (string, error) {
	if c.upgradeBuilderName != "" && c.upgradeBuildID == "" {
		b, err := c.GetUpgradeBuilder()
		if err != nil {
			return "", err
		}
		id, err := b.GetLatestBuildID()
		if err != nil {
			return "", fmt.Errorf("failt to lookup build id: %s", err)
		}
		c.upgradeBuildID = id
	}

	return c.upgradeBuildID, nil
}

func (c *Config) GetDowngradeRepository(dir string) (*packages.Repository, error) {
	buildID, err := c.GetDowngradeBuildID()
	if err != nil {
		return nil, err
	}

	if buildID != "" {
		build, err := c.archiveConfig.BuildArchive().GetBuildByID(buildID, dir)
		if err != nil {
			return nil, err
		}

		return build.GetPackageRepository()
	}

	if c.downgradeFuchsiaBuildDir != "" {
		return packages.NewRepository(filepath.Join(c.downgradeFuchsiaBuildDir, "amber-files"))
	}

	return nil, fmt.Errorf("downgrade repository not specified")
}

func (c *Config) GetUpgradeRepository(dir string) (*packages.Repository, error) {
	buildID, err := c.GetUpgradeBuildID()
	if err != nil {
		return nil, err
	}

	if buildID != "" {
		build, err := c.archiveConfig.BuildArchive().GetBuildByID(buildID, dir)
		if err != nil {
			return nil, err
		}

		return build.GetPackageRepository()
	}

	if c.upgradeFuchsiaBuildDir != "" {
		return packages.NewRepository(filepath.Join(c.upgradeFuchsiaBuildDir, "amber-files"))
	}

	return nil, fmt.Errorf("upgrade repository not specified")
}

func (c *Config) GetDowngradePaver(dir string) (*paver.Paver, error) {
	sshPrivateKey, err := c.deviceConfig.SSHPrivateKey()
	if err != nil {
		return nil, err
	}
	sshPublicKey := sshPrivateKey.PublicKey()

	buildID, err := c.GetDowngradeBuildID()
	if err != nil {
		return nil, err
	}

	if buildID != "" {
		build, err := c.archiveConfig.BuildArchive().GetBuildByID(buildID, dir)
		if err != nil {
			return nil, err
		}

		return build.GetPaver(sshPublicKey)
	}

	if c.downgradeFuchsiaBuildDir != "" {
		return paver.NewPaver(
				filepath.Join(c.downgradeFuchsiaBuildDir, "pave-zedboot.sh"),
				filepath.Join(c.downgradeFuchsiaBuildDir, "pave.sh"),
				sshPublicKey),
			nil
	}

	return nil, fmt.Errorf("downgrade paver not specified")
}
