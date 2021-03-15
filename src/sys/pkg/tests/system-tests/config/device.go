// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"golang.org/x/crypto/ssh"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
)

type DeviceConfig struct {
	sshKeyFile       string
	deviceFinderPath string
	DeviceName       string
	deviceHostname   string
	sshPrivateKey    ssh.Signer
	SerialSocketPath string
}

func NewDeviceConfig(fs *flag.FlagSet) *DeviceConfig {
	c := &DeviceConfig{}

	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests")

	fs.StringVar(&c.sshKeyFile, "ssh-private-key", os.Getenv(constants.SSHKeyEnvKey), "SSH private key file that can access the device")
	fs.StringVar(&c.DeviceName, "device", os.Getenv(constants.NodenameEnvKey), "device name")
	fs.StringVar(&c.deviceHostname, "device-hostname", os.Getenv(constants.DeviceAddrEnvKey), "device hostname or IPv4/IPv6 address")
	fs.StringVar(&c.deviceFinderPath, "device-finder-path", filepath.Join(testDataPath, "device-finder"), "device-finder tool path")
	fs.StringVar(&c.SerialSocketPath, "device-serial", os.Getenv(constants.SerialSocketEnvKey), "device serial path")

	return c
}

func (c *DeviceConfig) DeviceHostname(ctx context.Context) (string, error) {
	if c.deviceHostname != "" {
		return c.deviceHostname, nil
	}

	// The logic here should match get-fuchsia-device-addr (//tools/devshell/lib/vars.sh).
	if c.DeviceName == "" {
		var err error
		var deviceList string
		deviceList, err = c.DeviceFinder(ctx, "list", "-ipv4=false", "-timeout=1s", "-full")
		if err != nil {
			return "", fmt.Errorf("ERROR: Failed to list devices: %w", err)
		}
		if strings.Contains(deviceList, "\n") {
			return "", fmt.Errorf("ERROR: Found multiple devices. Use -device to specify one.")
		}
		// Device finder (with -full) outputs in the format "<address> <domain>".
		var entry = strings.Split(deviceList, " ")
		if len(entry) != 2 {
			return "", fmt.Errorf("ERROR: device-finder return unexpected output: %s.", deviceList)
		}
		c.deviceHostname = entry[0]
		c.DeviceName = entry[1]
	} else {
		var err error
		c.deviceHostname, err = c.DeviceFinder(ctx, "resolve", "-ipv4=false", "-timeout=1s", "-device-limit=1", c.DeviceName)
		if err != nil {
			return "", fmt.Errorf("ERROR: Failed to find device %s: %w", c.DeviceName, err)
		}
		if c.deviceHostname == "" {
			return "", fmt.Errorf("unable to determine the device hostname")
		}
	}
	return c.deviceHostname, nil
}

func (c *DeviceConfig) DeviceFinder(ctx context.Context, arg ...string) (string, error) {
	stdout, stderr, err := util.RunCommand(ctx, c.deviceFinderPath, arg...)
	if err != nil {
		return "", fmt.Errorf("device-finder failed: %w: %s", err, string(stderr))
	}
	return strings.TrimRight(string(stdout), "\n"), nil
}

func (c *DeviceConfig) SSHPrivateKey() (ssh.Signer, error) {
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

func (c *DeviceConfig) NewDeviceClient(ctx context.Context) (*device.Client, error) {
	deviceHostname, err := c.DeviceHostname(ctx)
	if err != nil {
		return nil, err
	}

	sshPrivateKey, err := c.SSHPrivateKey()
	if err != nil {
		return nil, err
	}

	return device.NewClient(ctx, deviceHostname, c.DeviceName, sshPrivateKey)
}
