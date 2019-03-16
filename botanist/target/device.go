// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"encoding/json"
	"fmt"
	"io/ioutil"

	"fuchsia.googlesource.com/tools/botanist/power"

	"golang.org/x/crypto/ssh"
)

// DeviceConfig contains the static properties of a target device.
type DeviceConfig struct {
	// Nodename is the hostname of the device that we want to boot on.
	Nodename string `json:"nodename"`

	// Power is the attached power management configuration.
	Power *power.Client `json:"power,omitempty"`

	// SSHKeys are the default system keys to be used with the device.
	SSHKeys []string `json:"keys,omitempty"`
}

// LoadDeviceConfigs unmarshalls a slice of DeviceConfigs from a given file.
func LoadDeviceConfigs(path string) ([]DeviceConfig, error) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read device properties file %q", path)
	}

	var configs []DeviceConfig
	if err := json.Unmarshal(data, &configs); err != nil {
		return nil, fmt.Errorf("failed to unmarshal configs: %v", err)
	}
	return configs, nil
}

// Returns the SSH signers associated with the key paths in the botanist config file if present.
func SSHSignersFromConfigs(configs []DeviceConfig) ([]ssh.Signer, error) {
	processedKeys := make(map[string]bool)
	var signers []ssh.Signer
	for _, config := range configs {
		for _, keyPath := range config.SSHKeys {
			if !processedKeys[keyPath] {
				processedKeys[keyPath] = true
				p, err := ioutil.ReadFile(keyPath)
				if err != nil {
					return nil, err
				}
				s, err := ssh.ParsePrivateKey(p)
				if err != nil {
					return nil, err
				}
				signers = append(signers, s)
			}
		}
	}
	return signers, nil
}
