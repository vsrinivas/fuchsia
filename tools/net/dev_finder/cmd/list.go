// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"strings"

	"github.com/google/subcommands"
)

type listCmd struct {
	devFinderCmd

	// Determines whether or not to print the full device info.
	fullInfo bool
	// Filters domains that match this string when listing devices.
	domainFilter string
}

func (*listCmd) Name() string {
	return "list"
}

func (*listCmd) Usage() string {
	return "list [flags...]\n\nflags:\n"
}

func (*listCmd) Synopsis() string {
	return "lists all Fuchsia devices on the network"
}

func (cmd *listCmd) SetFlags(f *flag.FlagSet) {
	cmd.SetCommonFlags(f)
	f.StringVar(&cmd.domainFilter, "domain-filter", "", "When using the \"list\" command, returns only devices that match this domain name.")
	f.BoolVar(&cmd.fullInfo, "full", false, "Print device address and domain")
}

func (cmd *listCmd) listDevices(ctx context.Context) ([]*fuchsiaDevice, error) {
	f := make(chan *fuchsiaDevice)
	for _, finder := range cmd.deviceFinders() {
		if err := finder.list(ctx, f); err != nil {
			return nil, err
		}
	}
	devices, err := cmd.filterInboundDevices(ctx, f)
	if err != nil {
		return nil, err
	}
	if len(devices) == 0 {
		return nil, fmt.Errorf("no devices found")
	}
	var filteredDevices []*fuchsiaDevice
	for _, device := range devices {
		if strings.Contains(device.domain, cmd.domainFilter) {
			filteredDevices = append(filteredDevices, device)
		}
	}
	if len(filteredDevices) == 0 {
		return nil, fmt.Errorf("no devices with domain matching '%v'", cmd.domainFilter)
	}
	return filteredDevices, nil
}

func (cmd *listCmd) execute(ctx context.Context) error {
	cmd.mdnsHandler = listMDNSHandler
	filteredDevices, err := cmd.listDevices(ctx)
	if err != nil {
		return err
	}

	if cmd.json {
		return cmd.outputJSON(filteredDevices, cmd.fullInfo)
	}
	return cmd.outputNormal(filteredDevices, cmd.fullInfo)
}

func (cmd *listCmd) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := cmd.execute(ctx); err != nil {
		log.Print(err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
