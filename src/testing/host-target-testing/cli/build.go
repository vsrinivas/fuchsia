// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cli

import (
	"context"
	"flag"
	"fmt"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/artifacts"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/device"
)

type BuildConfig struct {
	archiveConfig    *ArchiveConfig
	deviceConfig     *DeviceConfig
	prefix           string
	builderName      string
	buildID          string
	fuchsiaBuildDir  string
	productBundleDir string
}

func NewBuildConfig(
	fs *flag.FlagSet,
	archiveConfig *ArchiveConfig,
	deviceConfig *DeviceConfig,
	defaultBuildID string,
) *BuildConfig {
	return NewBuildConfigWithPrefix(fs, archiveConfig, deviceConfig, defaultBuildID, "")
}

func NewBuildConfigWithPrefix(
	fs *flag.FlagSet,
	archiveConfig *ArchiveConfig,
	deviceConfig *DeviceConfig,
	defaultBuildID string,
	prefix string,
) *BuildConfig {
	c := &BuildConfig{
		prefix:        prefix,
		archiveConfig: archiveConfig,
		deviceConfig:  deviceConfig,
	}

	fs.StringVar(&c.builderName, fmt.Sprintf("%sbuilder-name", prefix), "", "Pave to the latest version of this builder")
	fs.StringVar(&c.buildID, fmt.Sprintf("%sbuild-id", prefix), defaultBuildID, "Pave to this specific build id")
	fs.StringVar(&c.fuchsiaBuildDir, fmt.Sprintf("%sfuchsia-build-dir", prefix), "", "Pave to the build in this fuchsia build output directory")
	fs.StringVar(&c.productBundleDir, fmt.Sprintf("%sproduct-bundle-dir", prefix), "", "Flash to the build in this product bundle directory")

	return c
}

func (c *BuildConfig) Validate() error {
	defined := 0
	for _, s := range []string{
		c.builderName,
		c.buildID,
		c.fuchsiaBuildDir,
		c.productBundleDir,
	} {
		if s != "" {
			defined += 1
		}
	}
	if defined > 1 {
		return fmt.Errorf("--%sbuilder-name, --%sbuild-id, --%sfuchsia-build-dir, and %sproduct-bundle-dir are mutually exclusive",
			c.prefix,
			c.prefix,
			c.prefix,
			c.prefix,
		)
	}

	return nil
}

func (c *BuildConfig) GetBuilder() (*artifacts.Builder, error) {
	if c.builderName == "" {
		return nil, fmt.Errorf("builder not specified")
	}

	return c.archiveConfig.BuildArchive().GetBuilder(c.builderName), nil
}

func (c *BuildConfig) getBuildID(ctx context.Context) (string, error) {
	if c.builderName != "" && c.buildID == "" {
		b, err := c.GetBuilder()
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

func (c *BuildConfig) GetBuild(ctx context.Context, deviceClient *device.Client, dir string) (artifacts.Build, error) {
	sshPrivateKey, err := c.deviceConfig.SSHPrivateKey()
	if err != nil {
		return nil, fmt.Errorf("failed to get ssh key: %w", err)
	}

	buildID, err := c.getBuildID(ctx)
	if err != nil {
		return nil, err
	}

	var build artifacts.Build
	if buildID != "" {
		build, err = c.archiveConfig.BuildArchive().GetBuildByID(ctx, buildID, dir, sshPrivateKey.PublicKey())
	} else if c.fuchsiaBuildDir != "" {
		build, err = artifacts.NewFuchsiaDirBuild(c.fuchsiaBuildDir, sshPrivateKey.PublicKey()), nil
	} else if c.productBundleDir != "" {
		build, err = artifacts.NewProductBundleDirBuild(c.productBundleDir, sshPrivateKey.PublicKey()), nil
	}

	if err != nil {
		return nil, err
	}

	return build, nil
}
