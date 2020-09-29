// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"

	"github.com/google/subcommands"
)

type resolveCmd struct {
	devFinderCmd
}

func (*resolveCmd) Name() string {
	return "resolve"
}

func (*resolveCmd) Usage() string {
	return "resolve [flags...] [domains...]\n\nflags:\n"
}

func (*resolveCmd) Synopsis() string {
	return "attempts to resolve all passed Fuchsia domain names on the network"
}

func (cmd *resolveCmd) SetFlags(f *flag.FlagSet) {
	cmd.SetCommonFlags(f)
}

func (cmd *resolveCmd) resolveDevices(ctx context.Context, domains ...string) ([]*fuchsiaDevice, error) {
	if len(domains) == 0 {
		return nil, errors.New("no domains supplied")
	}
	deviceFinders, err := cmd.deviceFinders()
	if err != nil {
		return nil, err
	}
	f := make(chan *fuchsiaDevice, 1024)
	for _, finder := range deviceFinders {
		if err := finder.resolve(ctx, f, domains...); err != nil {
			return nil, err
		}
	}
	devices, err := cmd.filterInboundDevices(ctx, f, domains...)
	if err != nil {
		return nil, err
	}
	if len(devices) == 0 {
		return nil, fmt.Errorf("no devices found for domains: %v", domains)
	}
	return devices, err
}

func (cmd *resolveCmd) execute(ctx context.Context, domains ...string) error {
	cmd.mdnsHandler = resolveMDNSHandler
	outDevices, err := cmd.resolveDevices(ctx, domains...)
	if err != nil {
		return err
	}

	if cmd.json {
		return cmd.outputJSON(outDevices, false /* includeDomain */)
	}
	return cmd.outputNormal(outDevices, false /* includeDomain */)
}

func (cmd *resolveCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := cmd.execute(ctx, f.Args()...); err != nil {
		log.Print(err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
