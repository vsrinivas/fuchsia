// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pdu

import (
	"fuchsia.googlesource.com/tools/pdu/amt"
	"fuchsia.googlesource.com/tools/pdu/wol"
)

// Controller machines use 192.168.42.1/24 for swarming bots
// This will broadcast to that entire subnet.
const botBroadcastAddr = "192.168.42.255:9"

// Controller machines have multiple interfaces, currently
// 'eno2' is used for swarming bots.
const botInterface = "eno2"

// Config represents a PDU configuration for a particular device.
type Config struct {
	// Type is the type of PDU to use.
	Type string `json:"type"`

	// Host is the network hostname of the PDU e.g. fuchsia-tests-pdu-001.
	Host string `json:"host"`

	// HostHwAddr is the ethernet MAC address of the PDU e.g. 10:10:10:10:10:10
	HostMACAddr string `json:"host_mac_addr"`

	// Username is the username used to log in to the PDU.
	Username string `json:"username"`

	// Password is the password used to log in to the PDU.
	Password string `json:"password"`

	// DevicePath is the path to the device on the local machine.
	DevicePath string `json:"device_path"`

	// DevicePort is the PDU-specific string which identifies the
	// hardware device we're testing on in the PDU.
	DevicePort string `json:"device_port"`
}

// RebootDevice uses the given configuration to reboot the device.
func RebootDevice(cfg *Config) error {
	switch cfg.Type {
	case "amt":
		return amt.Reboot(cfg.Host, cfg.Username, cfg.Password)
	case "wol":
		return wol.Reboot(botBroadcastAddr, botInterface, cfg.HostMACAddr)
	}
	return nil
}
