// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"context"
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type DeviceResolver interface {
	Name() string
	ResolveName(ctx context.Context) (string, error)
}

type ConstantHostResolver struct {
	name string
	host string
}

func NewConstantHostResolver(name string, host string) ConstantHostResolver {
	return ConstantHostResolver{
		name: name,
		host: host,
	}
}

func (r ConstantHostResolver) Name() string {
	return r.name
}

func (r ConstantHostResolver) ResolveName(ctx context.Context) (string, error) {
	return r.host, nil
}

// The logic here should match get-fuchsia-device-addr (//tools/devshell/lib/vars.sh).
type DeviceFinderResolver struct {
	deviceFinderPath string
	deviceName       string
}

func NewDeviceFinderResolver(ctx context.Context, deviceFinderPath string, deviceName string) (*DeviceFinderResolver, error) {
	if deviceName == "" {
		var err error
		var deviceList string
		deviceList, err = deviceFinder(
			ctx,
			deviceFinderPath,
			"list",
			"-ipv4=false",
			"-timeout=5s",
			"-full",
		)
		if err != nil {
			return nil, fmt.Errorf("ERROR: Failed to list devices: %w", err)
		}
		if strings.Contains(deviceList, "\n") {
			return nil, fmt.Errorf("ERROR: Found multiple devices. Use -device to specify one.")
		}
		// Device finder (with -full) outputs in the format "<address> <domain>".
		var entry = strings.Split(deviceList, " ")
		if len(entry) != 2 {
			return nil, fmt.Errorf("ERROR: device-finder return unexpected output: %s.", deviceList)
		}
		deviceName = entry[1]

		if deviceName == "" {
			return nil, fmt.Errorf("unable to determine the device name")
		}
	}

	return &DeviceFinderResolver{
		deviceFinderPath: deviceFinderPath,
		deviceName:       deviceName,
	}, nil
}

func (r *DeviceFinderResolver) Name() string {
	return r.deviceName
}

func (r *DeviceFinderResolver) ResolveName(ctx context.Context) (string, error) {
	deviceHostname, err := deviceFinder(
		ctx,
		r.deviceFinderPath,
		"resolve",
		"-ipv4=false",
		"-timeout=5s",
		"-device-limit=1",
		r.deviceName,
	)
	if err != nil {
		return "", fmt.Errorf("ERROR: Failed to resolve device name %q: %w", r.deviceName, err)
	}

	logger.Infof(ctx, "resolved %q to %q", r.deviceName, deviceHostname)

	if deviceHostname == "" {
		return "", fmt.Errorf("unable to determine the device hostname")
	}

	return deviceHostname, nil
}

func deviceFinder(ctx context.Context, deviceFinderPath string, arg ...string) (string, error) {
	stdout, stderr, err := util.RunCommand(ctx, deviceFinderPath, arg...)
	if err != nil {
		return "", fmt.Errorf("device-finder failed: %w: %s", err, string(stderr))
	}
	return strings.TrimRight(string(stdout), "\n"), nil
}
