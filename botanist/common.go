// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
)

// DeviceProperties contains static properties of a hardware device.
type DeviceProperties struct {
	// Nodename is the hostname of the device that we want to boot on.
	Nodename string `json:"nodename"`

	// PDU is the configuration for the attached Power Distribution Unit.
	PDU *Config `json:"pdu,omitempty"`

	// SSHKeys are the default system keys to be used with the device.
	SSHKeys []string `json:"keys,omitempty"`
}

// LoadDeviceProperties unmarshalls a slice of DeviceProperties from a given file.
// For backwards compatibility, it supports unmarshalling a single DeviceProperties object also
// TODO(IN-1028): Update all botanist configs to use JSON list format
func LoadDeviceProperties(path string) ([]DeviceProperties, error) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read device properties file %q", path)
	}

	var propertiesSlice []DeviceProperties

	if err := json.Unmarshal(data, &propertiesSlice); err != nil {
		var properties DeviceProperties
		if err := json.Unmarshal(data, &properties); err != nil {
			return nil, err
		}
		propertiesSlice = append(propertiesSlice, properties)
	}
	return propertiesSlice, nil
}
