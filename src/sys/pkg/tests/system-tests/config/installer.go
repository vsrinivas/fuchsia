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

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/artifacts"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/avb"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/omaha"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/updater"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/zbi"
)

type InstallerMode = string

const (
	// Install OTAs with the omaha-client.
	Omaha = "omaha"

	// Install OTAs with the system-update-checker.
	SystemUpdateChecker = "system-update-checker"

	// The default fuchsia update package URL.
	defaultUpdatePackageURL = "fuchsia-pkg://fuchsia.com/update/0"
)

type InstallerConfig struct {
	installerMode   InstallerMode
	avbToolPath     string
	zbiToolPath     string
	keyPath         string
	keyMetadataPath string
	avbTool         *avb.AVBTool
	zbiTool         *zbi.ZBITool
	updater         updater.Updater
	omahaServer     *omaha.OmahaServer
	omahaAddress    string
}

func NewInstallerConfig(fs *flag.FlagSet) (*InstallerConfig, error) {
	c := &InstallerConfig{}

	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests")

	fs.StringVar(&c.installerMode, "installer", SystemUpdateChecker, "the installation mode (default: system-update-checker)")
	fs.StringVar(&c.avbToolPath, "avbtool-path", filepath.Join(testDataPath, "avbtool.py"),
		"path to the avbtool binary")
	fs.StringVar(&c.zbiToolPath, "zbitool-path", filepath.Join(testDataPath, "zbi"),
		"path to the zbi binary")
	fs.StringVar(&c.keyPath, "vbmeta-key", filepath.Join(testDataPath, "atx_psk.pem"), "path to the vbmeta private key")
	fs.StringVar(&c.keyMetadataPath, "vbmeta-key-metadata", filepath.Join(testDataPath, "avb_atx_metadata.bin"), "path to the vbmeta public key metadata")
	fs.StringVar(&c.omahaAddress, "omaha-address", ":0", "which address to serve omaha server on (default random)")

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

func (c *InstallerConfig) ZBITool() (*zbi.ZBITool, error) {
	if c.zbiTool == nil {
		zbiTool, err := zbi.NewZBITool(c.zbiToolPath)
		if err != nil {
			return nil, err
		}
		c.zbiTool = zbiTool
	}

	return c.zbiTool, nil
}

// ConfigureBuild configures a build for the updater.
func (c *InstallerConfig) ConfigureBuild(ctx context.Context, device *device.Client, build artifacts.Build) (artifacts.Build, error) {
	switch c.installerMode {
	case Omaha:
		if c.omahaServer == nil {
			localHostname, err := device.GetSSHConnection(ctx)
			if err != nil {
				return nil, err
			}

			omahaServer, err := omaha.NewOmahaServer(ctx, c.omahaAddress, localHostname)
			if err != nil {
				return nil, err
			}
			c.omahaServer = omahaServer
		}

		avbTool, err := c.AVBTool()
		if err != nil {
			return nil, err
		}

		zbiTool, err := c.ZBITool()
		if err != nil {
			return nil, err
		}

		return artifacts.NewOmahaBuild(build, c.omahaServer.URL(), avbTool, zbiTool), nil

	case SystemUpdateChecker:
		return build, nil

	default:
		return nil, errors.New("Invalid installer mode")
	}
}

// Updater returns the configured updater.
func (c *InstallerConfig) Updater(repo *packages.Repository, updatePackageURL string) (updater.Updater, error) {
	switch c.installerMode {
	case Omaha:
		avbTool, err := c.AVBTool()
		if err != nil {
			return nil, err
		}

		zbiTool, err := c.ZBITool()
		if err != nil {
			return nil, err
		}

		return updater.NewOmahaUpdater(repo, updatePackageURL, c.omahaServer, avbTool, zbiTool)

	case SystemUpdateChecker:
		// TODO: The e2e tests only support using the system-update-checker
		// with the standard update package URL. Otherwise we need to
		// fall back to manually triggering the system-updater.
		if updatePackageURL == defaultUpdatePackageURL {
			return updater.NewSystemUpdateChecker(repo), nil
		}

		return updater.NewSystemUpdater(repo, updatePackageURL), nil

	default:
		return nil, errors.New("Invalid installer mode")
	}
}
