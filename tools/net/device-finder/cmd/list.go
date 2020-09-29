// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"log"

	"github.com/google/subcommands"
)

type listCmd struct {
	devFinderCmd

	// Determines whether or not to print the full device info.
	fullInfo bool
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
	f.BoolVar(&cmd.fullInfo, "full", false, "Print device address and domain")
}

func (cmd *listCmd) listDevices(ctx context.Context) ([]*fuchsiaDevice, error) {
	deviceFinders, err := cmd.deviceFinders()
	if err != nil {
		return nil, err
	}
	f := make(chan *fuchsiaDevice, 1024)
	for _, finder := range deviceFinders {
		if err := finder.list(ctx, f); err != nil {
			return nil, err
		}
	}
	devices, err := cmd.filterInboundDevices(ctx, f)
	if err != nil {
		return nil, err
	}

	return devices, nil
}

func (cmd *listCmd) execute(ctx context.Context) error {
	cmd.mdnsHandler = listMDNSHandler
	devices, err := cmd.listDevices(ctx)
	if err != nil {
		return err
	}

	if cmd.json {
		return cmd.outputJSON(devices, cmd.fullInfo)
	}
	return cmd.outputNormal(devices, cmd.fullInfo)
}

func (cmd *listCmd) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := cmd.execute(ctx); err != nil {
		log.Print(err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
