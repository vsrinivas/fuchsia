// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package upgrade

import (
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/host_target_testing/artifacts"
	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/paver"
	"fuchsia.googlesource.com/host_target_testing/util"

	"golang.org/x/crypto/ssh"
)

type Config struct {
	OutputDir              string
	FuchsiaDir             string
	sshKeyFile             string
	netaddrPath            string
	DeviceName             string
	deviceHostname         string
	LkgbPath               string
	ArtifactsPath          string
	PackagesPath           string
	downgradeBuilderName   string
	downgradeBuildID       string
	downgradeAmberFilesDir string
	downgradePaver         string
	upgradeBuilderName     string
	upgradeBuildID         string
	upgradeAmberFilesDir   string
	cleanupOutputDir       bool
	archive                *artifacts.Archive
	sshPrivateKey          ssh.Signer
}

func NewConfig(fs *flag.FlagSet) (*Config, error) {
	c := &Config{}

	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system_ota_tests")

	fs.StringVar(&c.OutputDir, "output-dir", "", "save temporary files to this directory, defaults to a tempdir")
	fs.StringVar(&c.FuchsiaDir, "fuchsia-dir", os.Getenv("FUCHSIA_DIR"), "fuchsia dir")
	fs.StringVar(&c.sshKeyFile, "ssh-private-key", os.Getenv("FUCHSIA_SSH_KEY"), "SSH private key file that can access the device")
	fs.StringVar(&c.netaddrPath, "netaddr-path", filepath.Join(testDataPath, "netaddr"), "zircon netaddr tool path")
	fs.StringVar(&c.DeviceName, "device", os.Getenv("FUCHSIA_NODENAME"), "device name")
	fs.StringVar(&c.deviceHostname, "device-hostname", os.Getenv("FUCHSIA_IPV4_ADDR"), "device hostname or IPv4/IPv6 address")
	fs.StringVar(&c.LkgbPath, "lkgb", filepath.Join(testDataPath, "lkgb"), "path to lkgb, default is $FUCHSIA_DIR/prebuilt/tools/lkgb/lkgb")
	fs.StringVar(&c.ArtifactsPath, "artifacts", filepath.Join(testDataPath, "artifacts"), "path to the artifacts binary, default is $FUCHSIA_DIR/prebuilt/tools/artifacts/artifacts")
	fs.StringVar(&c.downgradeBuilderName, "downgrade-builder-name", "", "downgrade to the latest version of this builder")
	fs.StringVar(&c.downgradeBuildID, "downgrade-build-id", "", "downgrade to this specific build id")
	fs.StringVar(&c.downgradeAmberFilesDir, "downgrade-amber-files", "", "Path to the downgrade amber-files repository")
	fs.StringVar(&c.downgradePaver, "downgrade-paver", "", "Path to the downgrade paver.sh script")
	fs.StringVar(&c.upgradeBuilderName, "upgrade-builder-name", "", "upgrade to the latest version of this builder")
	fs.StringVar(&c.upgradeBuildID, "upgrade-build-id", os.Getenv("BUILDBUCKET_ID"), "upgrade to this build id (default is $BUILDBUCKET_ID)")
	fs.StringVar(&c.upgradeAmberFilesDir, "upgrade-amber-files", "", "Path to the upgrade amber-files repository")

	if c.OutputDir == "" {
		outputDir, err := ioutil.TempDir("", "system_ota_tests")
		if err != nil {
			return nil, fmt.Errorf("failed to create a temporary directory: %s", err)
		}
		c.OutputDir = outputDir
		c.cleanupOutputDir = true
	}

	return c, nil
}

func (c *Config) Validate() error {
	defined := 0
	for _, s := range []string{c.downgradeBuilderName, c.downgradeBuildID, c.downgradeAmberFilesDir} {
		if s != "" {
			defined += 1
		}
	}
	if defined != 1 {
		return fmt.Errorf("exactly one of -downgrade-builder-name, -downgrade-build-id, or -downgrade-amber-files must be specified")
	}

	defined = 0
	for _, s := range []string{c.upgradeBuilderName, c.upgradeBuildID, c.upgradeAmberFilesDir} {
		if s != "" {
			defined += 1
		}
	}
	if defined != 1 {
		return fmt.Errorf("exactly one of -upgrade-builder-name, -upgrade-build-id, or -upgrade-amber-files must be specified")
	}

	return nil
}

func (c *Config) Close() {
	if c.cleanupOutputDir {
		os.RemoveAll(c.OutputDir)
	}
}

func (c *Config) SshPrivateKey() (ssh.Signer, error) {
	if c.sshPrivateKey == nil {
		if c.sshKeyFile == "" {
			return nil, fmt.Errorf("ssh private key cannot be empty")
		}

		key, err := ioutil.ReadFile(c.sshKeyFile)
		if err != nil {
			return nil, err
		}

		privateKey, err := ssh.ParsePrivateKey(key)
		if err != nil {
			return nil, err
		}
		c.sshPrivateKey = privateKey
	}

	return c.sshPrivateKey, nil
}

func (c *Config) NewDeviceClient() (*device.Client, error) {
	deviceHostname, err := c.DeviceHostname()
	if err != nil {
		return nil, err
	}

	sshPrivateKey, err := c.SshPrivateKey()
	if err != nil {
		return nil, err
	}

	return device.NewClient(deviceHostname, sshPrivateKey)
}

func (c *Config) BuildArchive() *artifacts.Archive {
	if c.archive == nil {
		// Connect to the build archive service.
		c.archive = artifacts.NewArchive(c.LkgbPath, c.ArtifactsPath, c.OutputDir)
	}

	return c.archive
}

func (c *Config) GetDowngradeBuildID() (string, error) {
	if c.downgradeBuilderName != "" && c.downgradeBuildID == "" {
		a := c.BuildArchive()
		id, err := a.LookupBuildID(c.downgradeBuilderName)
		if err != nil {
			return "", fmt.Errorf("failed to lookup build id: %s", err)
		}
		c.downgradeBuildID = id
	}

	return c.downgradeBuildID, nil
}

func (c *Config) GetUpgradeBuildID() (string, error) {
	if c.upgradeBuilderName != "" && c.upgradeBuildID == "" {
		a := c.BuildArchive()
		id, err := a.LookupBuildID(c.upgradeBuilderName)
		if err != nil {
			return "", fmt.Errorf("failed to lookup build id: %s", err)
		}
		c.upgradeBuildID = id
	}

	return c.upgradeBuildID, nil
}

func (c *Config) GetDowngradeRepository() (*packages.Repository, error) {
	buildID, err := c.GetDowngradeBuildID()
	if err != nil {
		return nil, err
	}

	if buildID != "" {
		build, err := c.BuildArchive().GetBuildByID(buildID)
		if err != nil {
			return nil, err
		}

		return build.GetPackageRepository()
	}

	return packages.NewRepository(c.downgradeAmberFilesDir)
}

func (c *Config) GetUpgradeRepository() (*packages.Repository, error) {
	buildID, err := c.GetUpgradeBuildID()
	if err != nil {
		return nil, err
	}

	if buildID != "" {
		build, err := c.BuildArchive().GetBuildByID(buildID)
		if err != nil {
			return nil, err
		}

		return build.GetPackageRepository()
	}

	return packages.NewRepository(c.upgradeAmberFilesDir)
}

func (c *Config) GetDowngradePaver() (*paver.Paver, error) {
	sshPrivateKey, err := c.SshPrivateKey()
	if err != nil {
		return nil, err
	}
	sshPublicKey := sshPrivateKey.PublicKey()

	buildID, err := c.GetDowngradeBuildID()
	if err != nil {
		return nil, err
	}

	if buildID != "" {
		build, err := c.BuildArchive().GetBuildByID(buildID)
		if err != nil {
			return nil, err
		}

		return build.GetPaver(sshPublicKey)
	}

	return paver.NewPaver(c.downgradePaver, sshPublicKey), nil
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
