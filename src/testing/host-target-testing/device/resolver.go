// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"context"
	"encoding/json"
	"fmt"
	"os/exec"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type DeviceResolver interface {
	// NodeNames returns a list of nodenames for a device.
	NodeNames() []string

	// Resolve the device's nodename into a hostname.
	ResolveName(ctx context.Context) (string, error)

	// Block until the device appears to be in netboot.
	WaitToFindDeviceInNetboot(ctx context.Context) (string, error)
}

// ConstnatAddressResolver returns a fixed hostname for the specified nodename.
type ConstantHostResolver struct {
	nodeName    string
	oldNodeName string
	host        string
}

// NewConstantAddressResolver constructs a fixed host.
func NewConstantHostResolver(ctx context.Context, nodeName string, host string) ConstantHostResolver {
	oldNodeName, err := translateToOldNodeName(nodeName)
	if err == nil {
		logger.Infof(ctx, "translated device name %q to old nodename %q", nodeName, oldNodeName)
	} else {
		logger.Warningf(ctx, "could not translate %q into old device name: %v", nodeName, err)
		oldNodeName = ""
	}

	return ConstantHostResolver{
		nodeName:    nodeName,
		oldNodeName: oldNodeName,
		host:        host,
	}
}

func (r ConstantHostResolver) NodeNames() []string {
	if r.oldNodeName == "" {
		return []string{r.nodeName}
	} else {
		return []string{r.nodeName, r.oldNodeName}
	}
}

func (r ConstantHostResolver) ResolveName(ctx context.Context) (string, error) {
	return r.host, nil
}

func (r ConstantHostResolver) WaitToFindDeviceInNetboot(ctx context.Context) (string, error) {
	// We have no way to tell if the device is in netboot, so just exit.
	logger.Warningf(ctx, "ConstantHostResolver cannot tell if device is in netboot, assuming nodename is %s", r.nodeName)
	return r.nodeName, nil
}

// DeviceFinderResolver uses `device-finder` to resolve a nodename into a hostname.
// The logic here should match get-fuchsia-device-addr (//tools/devshell/lib/vars.sh).
type DeviceFinderResolver struct {
	deviceFinder *DeviceFinder
	nodeName     string
	oldNodeName  string
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

	var oldNodeName string

	// FIXME(http://fxbug.dev/71146) we can switch back to the resolver
	// resolving a single name after we no longer support testing builds
	// older than 2021-02-22.
	if name, err := translateToOldNodeName(nodeName); err == nil {
		logger.Infof(ctx, "translated device name %q to old nodename %q", nodeName, name)
		oldNodeName = name
	} else {
		logger.Warningf(ctx, "could not translate %q into old device name: %v", nodeName, err)
	}

	return &DeviceFinderResolver{
		deviceFinder: deviceFinder,
		nodeName:     nodeName,
		oldNodeName:  oldNodeName,
	}, nil
}

func (r *DeviceFinderResolver) NodeNames() []string {
	return []string{r.nodeName, r.oldNodeName}
}

func (r *DeviceFinderResolver) ResolveName(ctx context.Context) (string, error) {
	entry, err := r.deviceFinder.Resolve(ctx, Mdns, r.NodeNames())
	if err != nil {
		return "", err
	}

	return entry.Address, nil
}

func (r *DeviceFinderResolver) WaitToFindDeviceInNetboot(ctx context.Context) (string, error) {
	nodeNames := r.NodeNames()

	// Wait for the device to be listening in netboot.
	logger.Infof(ctx, "waiting for the to be listening on the nodenames: %v", nodeNames)

	attempt := 0
	for {
		attempt += 1

		if entry, err := r.deviceFinder.Resolve(ctx, Netboot, nodeNames); err == nil {
			logger.Infof(ctx, "device %v is listening on %q", nodeNames, entry)
			return entry.NodeName, nil
		} else {
			logger.Infof(ctx, "attempt %d failed to resolve nodenames %v: %v", attempt, nodeNames, err)
		}
	}
}

// FfxResolver uses `ffx target list` to resolve a nodename into a hostname.
type FfxResolver struct {
	ffxPath     string
	nodeName    string
	oldNodeName string
}

// NewFffResolver constructs a new `FfxResolver` for the specific nodename.
func NewFfxResolver(ctx context.Context, ffxPath string, nodeName string) (*FfxResolver, error) {
	if nodeName == "" {
		entries, err := ffxTargetList(ctx, ffxPath)

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

	var oldNodeName string

	// FIXME(http://fxbug.dev/71146) we can switch back to the resolver
	// resolving a single name after we no longer support testing builds
	// older than 2021-02-22.
	if name, err := translateToOldNodeName(nodeName); err == nil {
		logger.Infof(ctx, "translated device name %q to old nodename %q", nodeName, name)
		oldNodeName = name
	} else {
		logger.Warningf(ctx, "could not translate %q into old device name: %v", nodeName, err)
	}

	return &FfxResolver{
		ffxPath:     ffxPath,
		nodeName:    nodeName,
		oldNodeName: oldNodeName,
	}, nil
}

type targetEntry struct {
	NodeName    string   `json:"nodename"`
	Addresses   []string `json:"addresses"`
	TargetState string   `json:"target_state"`
}

func ffxTargetList(ctx context.Context, ffxPath string) ([]targetEntry, error) {
	args := []string{
		"--machine",
		"json",
		"target",
		"list",
	}

	stdout, stderr, err := util.RunCommand(ctx, ffxPath, args...)
	if err != nil {
		return []targetEntry{}, fmt.Errorf("ffx target list failed: %w: %s", err, string(stderr))
	}

	if len(stdout) == 0 {
		return []targetEntry{}, nil
	}

	var entries []targetEntry
	if err := json.Unmarshal(stdout, &entries); err != nil {
		return []targetEntry{}, err
	}

	return entries, nil
}

func ffxTargetListForNode(ctx context.Context, ffxPath string, nodeNames []string) ([]targetEntry, error) {
	entries, err := ffxTargetList(ctx, ffxPath)
	if err != nil {
		return []targetEntry{}, err
	}

	if len(nodeNames) == 0 {
		return entries, nil
	}

	var matchingTargets []targetEntry

	for _, target := range entries {
		for _, nodeName := range nodeNames {
			if target.NodeName == nodeName {
				matchingTargets = append(matchingTargets, target)
			}
		}
	}

	return matchingTargets, nil
}

func (r *FfxResolver) NodeNames() []string {
	return []string{r.nodeName, r.oldNodeName}
}

func (r *FfxResolver) ResolveName(ctx context.Context) (string, error) {
	nodeNames := r.NodeNames()
	logger.Infof(ctx, "resolving the nodenames %v", nodeNames)

	targets, err := ffxTargetListForNode(ctx, r.ffxPath, nodeNames)
	if err != nil {
		return "", err
	}

	logger.Infof(ctx, "resolved the nodenames %v to %v", nodeNames, targets)

	if len(targets) == 0 {
		return "", fmt.Errorf("no addresses found for nodenames: %v", nodeNames)
	}

	if len(targets) > 1 {
		return "", fmt.Errorf("multiple addresses found for nodenames %v: %v", nodeNames, targets)
	}

	return targets[0].Addresses[0], nil
}

func (r *FfxResolver) ffxSupportsZedbootDiscovery(ctx context.Context) (bool, error) {
	// Check if ffx is configured to resolve devices in zedboot.
	args := []string{
		"config",
		"get",
		"discovery.zedboot.enabled",
	}

	stdout, stderr, err := util.RunCommand(ctx, r.ffxPath, args...)
	if err != nil {
		// `ffx config get` exits with 2 if variable is undefined.
		if exiterr, ok := err.(*exec.ExitError); ok {
			if exiterr.ExitCode() == 2 {
				return false, nil
			}
		}

		return false, fmt.Errorf("ffx config get failed: %w: %s", err, string(stderr))
	}

	// FIXME(fxbug.dev/109280): Unfortunately we need to parse the raw string to see if it's true.
	if string(stdout) == "true\n" {
		return true, nil
	}

	return false, nil
}

func (r *FfxResolver) WaitToFindDeviceInNetboot(ctx context.Context) (string, error) {
	// Exit early if ffx is not configured to listen for devices in zedboot.
	supportsZedbootDiscovery, err := r.ffxSupportsZedbootDiscovery(ctx)
	if err != nil {
		return "", err
	}

	if !supportsZedbootDiscovery {
		logger.Warningf(ctx, "ffx not configured to listen for devices in zedboot, assuming nodename is %s", r.nodeName)
		return r.nodeName, nil
	}

	nodeNames := r.NodeNames()

	// Wait for the device to be listening in netboot.
	logger.Infof(ctx, "waiting for the to be listening on the nodenames: %v", nodeNames)

	attempt := 0
	for {
		attempt += 1

		if entries, err := ffxTargetListForNode(ctx, r.ffxPath, nodeNames); err == nil {
			for _, entry := range entries {
				logger.Infof(ctx, "device is in %v", entry.TargetState)
				if entry.TargetState == "Zedboot (R)" {
					logger.Infof(ctx, "device %v is listening on %q", entry.NodeName, entry)
					return entry.NodeName, nil
				}
			}

			logger.Infof(ctx, "attempt %d waiting for device to boot into zedboot", attempt)
			time.Sleep(5 * time.Second)
		} else {
			logger.Infof(ctx, "attempt %d failed to resolve nodenames %v: %v", attempt, nodeNames, err)
		}

	}
}
