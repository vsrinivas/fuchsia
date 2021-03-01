// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// DeviceFinderMode selects the protocol to use to resolve a nodename to an IP Address.
type DeviceFinderMode int

const (
	// Mdns uses the mDNS protocol to resolve a nodename to an IP address.
	Mdns DeviceFinderMode = iota

	// Netboot uses the netboot protocol to resolve a nodename to an IP address.
	Netboot
)

func (m DeviceFinderMode) String() string {
	if m == Mdns {
		return "mdns"
	}

	return "netboot"
}

// DeviceFinder wraps the logic around the `device-finder` tool. This tools
// allows the discovery and resolution of device node names to their IP
// addresses.
type DeviceFinder struct {
	deviceFinderPath string
}

// NewDeviceFinder constructs a `DeviceFinder` type wrapping the
// `device-finder` path pointed to by the `deviceFinderPath`.
func NewDeviceFinder(deviceFinderPath string) *DeviceFinder {
	return &DeviceFinder{
		deviceFinderPath: deviceFinderPath,
	}
}

// DeviceEntry describes the result from `List` and `ListNetboot`.
type DeviceEntry struct {
	Address  string
	NodeName string
}

// List wraps running `device-finder list ...`. This can be filtered down for specific nodenames.
func (d *DeviceFinder) List(ctx context.Context, mode DeviceFinderMode, nodeNames ...string) ([]DeviceEntry, error) {
	args := []string{
		"list",
		"-ipv4=false",
		"-timeout=5s",
		"-full",
	}
	switch mode {
	case Mdns:
		args = append(args, "-mdns")
	case Netboot:
		args = append(args, "-netboot")
	}

	stdout, err := d.run(ctx, args...)
	if err != nil {
		return nil, fmt.Errorf("failed to list devices: %w", err)
	}

	var devices []DeviceEntry

	s := bufio.NewScanner(bytes.NewReader(stdout))
	for s.Scan() {
		line := s.Text()
		// Device finder (with -full) outputs in the format "<address> <domain>".
		entry := strings.Split(line, " ")
		if len(entry) != 2 {
			return nil, fmt.Errorf("device-finder return unexpected output: %s", stdout)
		}

		address := entry[0]
		nodeName := entry[1]

		if address == "" {
			return nil, fmt.Errorf("device-finder list returned an empty address")
		}

		if nodeName == "" {
			return nil, fmt.Errorf("device-finder list returned an empty nodename")
		}

		devices = append(devices, DeviceEntry{
			Address:  address,
			NodeName: nodeName,
		})
	}

	return devices, nil
}

// Resolves nodenames to an IP address.
func (d *DeviceFinder) Resolve(ctx context.Context, mode DeviceFinderMode, nodeNames []string) (DeviceEntry, error) {
	logger.Infof(ctx, "resolving the nodenames %v using %v", nodeNames, mode)

	entries, err := d.List(ctx, mode)
	if err != nil {
		return DeviceEntry{}, err
	}

	var nodeEntries []DeviceEntry

	if len(nodeNames) == 0 {
		// Resolve to all addresses if no nodename is specified.
		for _, entry := range entries {
			nodeEntries = append(nodeEntries, entry)
		}
	} else {
		// Otherwise, filter the entry list for the nodenames we are interested in.
		for _, entry := range entries {
			for _, nodeName := range nodeNames {
				if entry.NodeName == nodeName {
					nodeEntries = append(nodeEntries, entry)
				}
			}
		}
	}

	logger.Infof(ctx, "resolved the nodenames %v to %v", nodeNames, entries)

	if len(nodeEntries) == 0 {
		return DeviceEntry{}, fmt.Errorf("no addresses found for nodenames: %v", nodeNames)
	}

	if len(nodeEntries) > 1 {
		return DeviceEntry{}, fmt.Errorf("multiple addresses found for nodenames %v: %v", nodeNames, entries)
	}

	return nodeEntries[0], nil
}

func (d *DeviceFinder) run(ctx context.Context, arg ...string) ([]byte, error) {
	stdout, stderr, err := util.RunCommand(ctx, d.deviceFinderPath, arg...)
	if err != nil {
		return []byte{}, fmt.Errorf("device-finder failed: %w: %s", err, string(stderr))
	}
	return stdout, nil
}
