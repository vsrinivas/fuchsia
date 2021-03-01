// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"context"
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type DeviceResolver interface {
	// NodeNames returns a list of nodenames for a device.
	NodeNames() []string

	// Resolve the device's nodename into a hostname.
	ResolveName(ctx context.Context) (string, error)
}

// ConstnatAddressResolver returns a fixed hostname for the specified nodename.
type ConstantHostResolver struct {
	nodeNames []string
	host      string
}

// NewConstantAddressResolver constructs a fixed host.
func NewConstantHostResolver(ctx context.Context, nodeName string, host string) ConstantHostResolver {
	nodeNames := []string{nodeName}

	if oldNodeName, err := translateToOldNodeName(nodeName); err == nil {
		logger.Infof(ctx, "translated device name %q to old nodename %q", nodeName, oldNodeName)
		nodeNames = append(nodeNames, oldNodeName)
	} else {
		logger.Warningf(ctx, "could not translate %q into old device name: %v", nodeName, err)
	}

	return ConstantHostResolver{
		nodeNames: nodeNames,
		host:      host,
	}
}

func (r ConstantHostResolver) NodeNames() []string {
	return r.nodeNames
}

func (r ConstantHostResolver) ResolveName(ctx context.Context) (string, error) {
	return r.host, nil
}

// DeviceFinderResolver uses `device-finder` to resolve a nodename into a hostname.
// The logic here should match get-fuchsia-device-addr (//tools/devshell/lib/vars.sh).
type DeviceFinderResolver struct {
	deviceFinder *DeviceFinder
	nodeNames    []string
}

// NewDeviceFinderResolver constructs a new `DeviceFinderResolver` for the specific
func NewDeviceFinderResolver(ctx context.Context, deviceFinder *DeviceFinder, nodeName string) (*DeviceFinderResolver, error) {
	if nodeName == "" {
		entries, err := deviceFinder.List(ctx, Mdns)
		if err != nil {
			return nil, fmt.Errorf("failed to list devices: %w", err)
		}
		if len(entries) == 0 {
			return nil, fmt.Errorf("no devices found")
		}

		if len(entries) != 1 {
			return nil, fmt.Errorf("cannot use empty nodename with multiple devices: %v", entries)
		}

		nodeName = entries[0].NodeName
	}

	nodeNames := []string{nodeName}

	// FIXME(http://fxbug.dev/71146) we can switch back to the resolver
	// resolving a single name after we no longer support testing builds
	// older than 2021-02-22.
	if oldNodeName, err := translateToOldNodeName(nodeName); err == nil {
		logger.Infof(ctx, "translated device name %q to old nodename %q", nodeName, oldNodeName)
		nodeNames = append(nodeNames, oldNodeName)
	} else {
		logger.Warningf(ctx, "could not translate %q into old device name: %v", nodeName, err)
	}

	return &DeviceFinderResolver{
		deviceFinder: deviceFinder,
		nodeNames:    nodeNames,
	}, nil
}

func (r *DeviceFinderResolver) NodeNames() []string {
	return r.nodeNames
}

func (r *DeviceFinderResolver) ResolveName(ctx context.Context) (string, error) {
	entry, err := r.deviceFinder.Resolve(ctx, Mdns, r.nodeNames)
	if err != nil {
		return "", err
	}

	return entry.Address, nil
}
