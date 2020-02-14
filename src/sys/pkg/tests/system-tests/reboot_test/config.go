// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package reboot

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/host_target_testing/artifacts"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/paver"

	systemTestConfig "fuchsia.googlesource.com/system_tests/config"
)

type config struct {
	archiveConfig   *systemTestConfig.ArchiveConfig
	deviceConfig    *systemTestConfig.DeviceConfig
	packagesPath    string
	builderName     string
	buildID         string
	fuchsiaBuildDir string
	paveTimeout     time.Duration
	cycleCount      int
	cycleTimeout    time.Duration
}

func newConfig(fs *flag.FlagSet) (*config, error) {
	c := &config{
		archiveConfig: systemTestConfig.NewArchiveConfig(fs),
		deviceConfig:  systemTestConfig.NewDeviceConfig(fs),
	}

	fs.StringVar(&c.builderName, "builder-name", "", "Pave to the latest version of this builder")
	fs.StringVar(&c.buildID, "build-id", os.Getenv("BUILDBUCKET_ID"), "Pave to this specific build id")
	fs.StringVar(&c.fuchsiaBuildDir, "fuchsia-build-dir", "", "Pave to the build in this directory")
	fs.IntVar(&c.cycleCount, "cycle-count", 1, "How many cycles to run the test before completing (default is 1)")
	fs.DurationVar(&c.paveTimeout, "pave-timeout", 5*time.Minute, "Err if a pave takes longer than this time (default 5 minutes)")
	fs.DurationVar(&c.cycleTimeout, "cycle-timeout", 5*time.Minute, "Err if a test cycle takes longer than this time (default is 5 minutes)")

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

func (c *config) shouldRepaveDevice() bool {
	return c.buildID != "" || c.builderName != "" || c.fuchsiaBuildDir != ""
}

func (c *config) getBuilder() (*artifacts.Builder, error) {
	if c.builderName == "" {
		return nil, fmt.Errorf("builder not specified")
	}

	return c.archiveConfig.BuildArchive().GetBuilder(c.builderName), nil
}

func (c *config) getBuildID() (string, error) {
	if c.builderName != "" && c.buildID == "" {
		b, err := c.getBuilder()
		if err != nil {
			return "", err
		}
		id, err := b.GetLatestBuildID()
		if err != nil {
			return "", fmt.Errorf("failed to lookup build id: %s", err)
		}
		c.buildID = id
	}

	return c.buildID, nil
}

func (c *config) getRepository(dir string) (*packages.Repository, error) {
	buildID, err := c.getBuildID()
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

	if c.fuchsiaBuildDir != "" {
		return packages.NewRepository(filepath.Join(c.fuchsiaBuildDir, "amber-files"))
	}

	return nil, fmt.Errorf("repository not specified")
}

func (c *config) getPaver(dir string) (*paver.Paver, error) {
	sshPrivateKey, err := c.deviceConfig.SSHPrivateKey()
	if err != nil {
		return nil, err
	}
	sshPublicKey := sshPrivateKey.PublicKey()

	buildID, err := c.getBuildID()
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

	if c.fuchsiaBuildDir != "" {
		return paver.NewPaver(
				filepath.Join(c.fuchsiaBuildDir, "pave-zedboot.sh"),
				filepath.Join(c.fuchsiaBuildDir, "pave.sh"),
				sshPublicKey),
			nil
	}

	return nil, fmt.Errorf("paver not specified")
}
