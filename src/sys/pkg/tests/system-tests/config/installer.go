// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"context"
	"errors"
	"flag"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/host_target_testing/artifacts"
	"fuchsia.googlesource.com/host_target_testing/avb"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/updater"
)

type InstallerMode = string

const (
	SystemUpdateChecker = "system-update-checker"
)

type InstallerConfig struct {
	installerMode   InstallerMode
	avbToolPath     string
	keyPath         string
	keyMetadataPath string
	avbTool         *avb.AVBTool
	updater         updater.Updater
}

func NewInstallerConfig(fs *flag.FlagSet) (*InstallerConfig, error) {
	c := &InstallerConfig{}

	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests")

	fs.StringVar(&c.installerMode, "installer", SystemUpdateChecker, "the installation mode (default: system-update-checker)")
	fs.StringVar(&c.avbToolPath, "avbtool-path", filepath.Join(testDataPath, "avbtool"),
		"path to the avbtool binary")
	fs.StringVar(&c.keyPath, "vbmeta-key", filepath.Join(testDataPath, "atx_psk.pem"), "path to the vbmeta private key")
	fs.StringVar(&c.keyMetadataPath, "vbmeta-key-metadata", filepath.Join(testDataPath, "avb_atx_metadata.bin"), "path to the vbmeta public key metadata")

	return c, nil
}

func (c *InstallerConfig) AVBTool() (*avb.AVBTool, error) {
	if c.avbTool == nil {
		avbTool, err := avb.NewAVBTool(c.avbToolPath, c.keyPath, c.keyMetadataPath)
		if err != nil {
			return nil, err
		}
		c.avbTool = avbTool
	}

	return c.avbTool, nil
}

func (c *InstallerConfig) ConfigureBuild(ctx context.Context, build artifacts.Build) (artifacts.Build, error) {
	switch c.installerMode {
	case SystemUpdateChecker:
		return build, nil
	default:
		return nil, errors.New("Invalid installer mode")
	}
}

func (c *InstallerConfig) Updater(repo *packages.Repository) (updater.Updater, error) {
	if c.updater == nil {
		switch c.installerMode {
		case SystemUpdateChecker:
			c.updater = updater.NewSystemUpdateChecker(repo)
		default:
			return nil, errors.New("Invalid installer mode")
		}
	}

	return c.updater, nil
}
