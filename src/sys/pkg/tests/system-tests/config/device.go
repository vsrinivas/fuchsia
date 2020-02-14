// Copyright 2020 The Fuchsia Authors. All rights reserved.
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

	"golang.org/x/crypto/ssh"

	"fuchsia.googlesource.com/host_target_testing/device"
	"fuchsia.googlesource.com/host_target_testing/util"
)

type DeviceConfig struct {
	sshKeyFile     string
	netaddrPath    string
	DeviceName     string
	deviceHostname string
	sshPrivateKey  ssh.Signer
}

func NewDeviceConfig(fs *flag.FlagSet) *DeviceConfig {
	c := &DeviceConfig{}

	testDataPath := filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests")

	fs.StringVar(&c.sshKeyFile, "ssh-private-key", os.Getenv("FUCHSIA_SSH_KEY"), "SSH private key file that can access the device")
	fs.StringVar(&c.DeviceName, "device", os.Getenv("FUCHSIA_NODENAME"), "device name")
	fs.StringVar(&c.deviceHostname, "device-hostname", os.Getenv("FUCHSIA_IPV4_ADDR"), "device hostname or IPv4/IPv6 address")
	fs.StringVar(&c.netaddrPath, "netaddr-path", filepath.Join(testDataPath, "netaddr"), "zircon netaddr tool path")

	return c
}

func (c *DeviceConfig) DeviceHostname() (string, error) {
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

func (c *DeviceConfig) netaddr(arg ...string) (string, error) {
	stdout, stderr, err := util.RunCommand(c.netaddrPath, arg...)
	if err != nil {
		return "", fmt.Errorf("netaddr failed: %s: %s", err, string(stderr))
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

func (c *DeviceConfig) NewDeviceClient() (*device.Client, error) {
	deviceHostname, err := c.DeviceHostname()
	if err != nil {
		return nil, err
	}

	sshPrivateKey, err := c.SSHPrivateKey()
	if err != nil {
		return nil, err
	}

	return device.NewClient(deviceHostname, sshPrivateKey)
}
