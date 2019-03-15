// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"os"

	"github.com/google/subcommands"

	"fuchsia.googlesource.com/tools/mdns"
)

const (
	ipv4AddrLength = 4
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

func resolveMDNSHandler(resp mDNSResponse, localResolve bool, devChan chan<- *fuchsiaDevice, errChan chan<- error) {
	for _, a := range resp.rxPacket.Answers {
		if a.Class == mdns.IN && a.Type == mdns.A &&
			len(a.Data) == ipv4AddrLength {
			if localResolve {
				recvIP, err := resp.getReceiveIP()
				if err != nil {
					errChan <- err
					return
				}
				devChan <- &fuchsiaDevice{recvIP, a.Domain}
				continue
			}
			devChan <- &fuchsiaDevice{net.IP(a.Data), a.Domain}
		}
	}
}

func (cmd *resolveCmd) execute(ctx context.Context, domains ...string) error {
	if len(domains) == 0 {
		return errors.New("no domains supplied")
	}

	var outDevices []*fuchsiaDevice
	for _, domain := range domains {
		mDNSDomain := fmt.Sprintf("%s.local", domain)
		devices, err := cmd.sendMDNSPacket(ctx, mdns.QuestionPacket(mDNSDomain))
		if err != nil {
			return fmt.Errorf("sending/receiving mdns packets during resolve of domain '%s': %v", domain, err)
		}
		filteredDevices := make([]*fuchsiaDevice, 0)
		for _, device := range devices {
			if device.domain == mDNSDomain {
				filteredDevices = append(filteredDevices, device)
			}
		}
		if len(filteredDevices) == 0 {
			return fmt.Errorf("no devices with domain %v", domain)
		}

		for _, device := range filteredDevices {
			outDevices = append(outDevices, device)
		}
	}

	if cmd.json {
		return cmd.outputJSON(outDevices)
	}
	return cmd.outputNormal(outDevices)
}

func (cmd *resolveCmd) outputNormal(devices []*fuchsiaDevice) error {
	for _, device := range devices {
		fmt.Println(device.addr)
	}
	return nil
}

func (cmd *resolveCmd) outputJSON(devices []*fuchsiaDevice) error {
	jsonOut := jsonOutput{Devices: make([]jsonDevice, 0, len(devices))}
	for _, device := range devices {
		jsonOut.Devices = append(jsonOut.Devices, jsonDevice{Addr: device.addr.String()})
	}

	e := json.NewEncoder(os.Stdout)
	e.SetIndent("", "  ")
	return e.Encode(jsonOut)
}

func (cmd *resolveCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	cmd.mdnsHandler = resolveMDNSHandler
	if err := cmd.execute(ctx, f.Args()...); err != nil {
		log.Print(err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
