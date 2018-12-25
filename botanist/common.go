// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"encoding/json"
	"os"
	"strings"

	"fuchsia.googlesource.com/tools/pdu"
)

// StringsFlag implements flag.Value so it may be treated as a flag type.
type StringsFlag []string

// Set implements flag.Value.Set.
func (s *StringsFlag) Set(val string) error {
	*s = append(*s, val)
	return nil
}

// Strings implements flag.Value.String.
func (s *StringsFlag) String() string {
	if s == nil {
		return ""
	}
	return strings.Join([]string(*s), ", ")
}

// DeviceProperties contains static properties of a hardware device.
type DeviceProperties struct {
	// Nodename is the hostname of the device that we want to boot on.
	Nodename string `json:"nodename"`

	// PDU is the configuration for the attached Power Distribution Unit.
	PDU *pdu.Config `json:"pdu,omitempty"`
}

// LoadDeviceProperties unmarshalls the DeviceProperties found in a given file.
func LoadDeviceProperties(propertiesFile string, properties *DeviceProperties) error {
	file, err := os.Open(propertiesFile)
	if err != nil {
		return err
	}

	return json.NewDecoder(file).Decode(properties)
}
