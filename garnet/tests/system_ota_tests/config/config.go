// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/system_ota_tests/build"
	"fuchsia.googlesource.com/system_ota_tests/device"
	"fuchsia.googlesource.com/system_ota_tests/util"
)

type Config struct {
	OutputDir      string
	FuchsiaDir     string
	SshKeyFile     string
	netaddrPath    string
	localHostname  string
	DeviceName     string
	deviceHostname string
	LkgbPath       string
	ArtifactsPath  string
	BuilderName    string
	buildID        string
	archive        *build.Archive
}

func NewConfig(fs *flag.FlagSet) (*Config, error) {
	outputDir, err := ioutil.TempDir("", "system_ota_tests")
	if err != nil {
		return nil, fmt.Errorf("failed to create a temporary directory: %s", err)
	}
	c := &Config{
		OutputDir: outputDir,
	}

	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system_ota_tests")

	fs.StringVar(&c.FuchsiaDir, "fuchsia-dir", os.Getenv("FUCHSIA_DIR"), "fuchsia dir")
	fs.StringVar(&c.SshKeyFile, "ssh-private-key", os.Getenv("FUCHSIA_SSH_KEY"), "SSH private key file that can access the device")
	fs.StringVar(&c.netaddrPath, "netaddr-path", filepath.Join(testDataPath, "netaddr"), "zircon netaddr tool path")
	fs.StringVar(&c.localHostname, "local-hostname", "", "local hostname")
	fs.StringVar(&c.DeviceName, "device", "", "device name")
	fs.StringVar(&c.deviceHostname, "device-hostname", os.Getenv("FUCHSIA_IPV4_ADDR"), "device hostname or IPv4/IPv6 address")
	fs.StringVar(&c.LkgbPath, "lkgb", filepath.Join(testDataPath, "lkgb"), "path to lkgb, default is $FUCHSIA_DIR/prebuilt/tools/lkgb/lkgb")
	fs.StringVar(&c.ArtifactsPath, "artifacts", filepath.Join(testDataPath, "artifacts"), "path to the artifacts binary, default is $FUCHSIA_DIR/prebuilt/tools/artifacts/artifacts")
	fs.StringVar(&c.BuilderName, "builder-name", "", "download the amber repository from the latest build of this builder")
	fs.StringVar(&c.buildID, "build-id", "", "download the amber repository from this build id")

	return c, nil
}

func (c *Config) Validate() error {
	if c.BuilderName == "" && c.buildID == "" {
		return fmt.Errorf("-builder-name or -build-id must be specified")
	}
	return nil
}

func (c *Config) Close() {
	os.RemoveAll(c.OutputDir)
}

func (c *Config) NewDeviceClient() (*device.Client, error) {
	deviceHostname, err := c.DeviceHostname()
	if err != nil {
		return nil, err
	}
	if c.SshKeyFile == "" {
		return nil, fmt.Errorf("ssh private key cannot be empty")
	}

	return device.NewClient(deviceHostname, c.SshKeyFile)
}

func (c *Config) BuildArchive() *build.Archive {
	if c.archive == nil {
		// Connect to the build archive service.
		c.archive = build.NewArchive(c.LkgbPath, c.ArtifactsPath, c.OutputDir)
	}

	return c.archive
}

func (c *Config) BuildID() (string, error) {
	if c.buildID == "" {
		a := c.BuildArchive()
		id, err := a.LookupBuildID(c.BuilderName)
		if err != nil {
			return "", fmt.Errorf("failed to lookup build id: %s", err)
		}
		c.buildID = id
	}

	return c.buildID, nil
}

func (c *Config) LocalHostname() (string, error) {
	if c.localHostname == "" {
		var err error
		c.localHostname, err = c.netaddr("--local", c.DeviceName)
		if err != nil {
			return "", fmt.Errorf("ERROR: netaddr failed: %s", err)
		}
		if c.localHostname == "" {
			return "", fmt.Errorf("unable to determine the local hostname")
		}
	}

	return c.localHostname, nil
}

func (c *Config) DeviceHostname() (string, error) {
	if c.deviceHostname == "" {
		var err error
		c.deviceHostname, err = c.netaddr("--nowait", "--timeout=1000", "--fuchsia", c.DeviceName)
		if err != nil {
			return "", fmt.Errorf("ERROR: netaddr failed: %s", err)
		}
		if c.deviceHostname == "" {
			return "", fmt.Errorf("unable to determine the device hostname")
		}
	}

	return c.deviceHostname, nil
}

func (c *Config) netaddr(arg ...string) (string, error) {
	stdout, stderr, err := util.RunCommand(c.netaddrPath, arg...)
	if err != nil {
		return "", fmt.Errorf("netaddr failed: %s: %s", err, string(stderr))
	}
	return strings.TrimRight(string(stdout), "\n"), nil
}
