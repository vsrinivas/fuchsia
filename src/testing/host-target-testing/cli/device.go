// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cli

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"time"

	"golang.org/x/crypto/ssh"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/device"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
)

type DeviceConfig struct {
	sshKeyFile               string
	deviceFinderPath         string
	deviceName               string
	deviceHostname           string
	sshPrivateKey            ssh.Signer
	SerialSocketPath         string
	connectTimeout           time.Duration
	WorkaroundBrokenTimeSkip bool
}

func NewDeviceConfig(fs *flag.FlagSet) *DeviceConfig {
	c := &DeviceConfig{}

	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests")

	fs.StringVar(&c.sshKeyFile, "ssh-private-key", os.Getenv(constants.SSHKeyEnvKey), "SSH private key file that can access the device")
	fs.StringVar(&c.deviceName, "device", os.Getenv(constants.NodenameEnvKey), "device name")
	fs.StringVar(&c.deviceHostname, "device-hostname", os.Getenv(constants.DeviceAddrEnvKey), "device hostname or IPv4/IPv6 address")
	fs.StringVar(&c.deviceFinderPath, "device-finder-path", filepath.Join(testDataPath, "device-finder"), "device-finder tool path")
	fs.StringVar(&c.SerialSocketPath, "device-serial", os.Getenv(constants.SerialSocketEnvKey), "device serial path")
	fs.DurationVar(&c.connectTimeout, "device-connect-timeout", 5*time.Second, "device connection timeout (default 5 seconds)")
	fs.BoolVar(&c.WorkaroundBrokenTimeSkip, "workaround-broken-time-skip", false,
		"whether to sleep for 15 seconds after pave and then reconnect, to work around a known networking bug, fxbug.dev/74861")

	return c
}

// newDeviceFinder constructs a DeviceFinder in order to help `device.Client` discover and resolve
// nodenames into IP addresses.
func (c *DeviceConfig) newDeviceFinder() (*device.DeviceFinder, error) {
	if c.deviceFinderPath == "" {
		return nil, fmt.Errorf("--device-finder-path not specified")
	}

	return device.NewDeviceFinder(c.deviceFinderPath), nil
}

func (c *DeviceConfig) DeviceResolver(ctx context.Context) (device.DeviceResolver, error) {
	if c.deviceHostname != "" {
		return device.NewConstantHostResolver(ctx, c.deviceName, c.deviceHostname), nil
	}

	deviceFinder, err := c.newDeviceFinder()
	if err != nil {
		return nil, err
	}

	return device.NewDeviceFinderResolver(ctx, deviceFinder, c.deviceName)
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
	deviceFinder, err := c.newDeviceFinder()
	if err != nil {
		return nil, err
	}

	deviceResolver, err := c.DeviceResolver(ctx)
	if err != nil {
		return nil, err
	}

	sshPrivateKey, err := c.SSHPrivateKey()
	if err != nil {
		return nil, err
	}

	connectBackoff := retry.NewConstantBackoff(c.connectTimeout)

	return device.NewClient(ctx, deviceFinder, deviceResolver, sshPrivateKey, connectBackoff, c.WorkaroundBrokenTimeSkip)
}
