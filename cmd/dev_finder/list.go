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

	"fuchsia.googlesource.com/tools/mdns"
)

const (
	fuchsiaService = "_fuchsia._udp.local"
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

func listMDNSHandler(resp mDNSResponse, localResolve bool, devChan chan<- *fuchsiaDevice, errChan chan<- error) {
	for _, a := range resp.rxPacket.Answers {
		if a.Class == mdns.IN && a.Type == mdns.PTR {
			// DX-1498: Some protection against malformed responses.
			if len(a.Data) == 0 {
				log.Print("Empty data in response. Ignoring...")
				continue
			}
			nameLength := int(a.Data[0])
			if len(a.Data) < nameLength+1 {
				log.Printf("Too short data in response. Got %d bytes; expected %d", len(a.Data), nameLength+1)
				continue
			}

			// This is a bit convoluted: the domain param is being used
			// as a "service", and the Data field actually contains the
			// domain of the device.
			fuchsiaDomain := string(a.Data[1 : nameLength+1])
			if localResolve {
				recvIP, err := resp.getReceiveIP()
				if err != nil {
					errChan <- err
					return
				}
				devChan <- &fuchsiaDevice{recvIP, fuchsiaDomain}
				continue
			}
			if ip, err := addrToIP(resp.devAddr); err != nil {
				errChan <- fmt.Errorf("could not find addr for %v: %v", resp.devAddr, err)
			} else {
				devChan <- &fuchsiaDevice{
					addr:   ip,
					domain: fuchsiaDomain,
				}
			}
		}
	}
}

func (cmd *listCmd) listDevices(ctx context.Context) ([]*fuchsiaDevice, error) {
	listPacket := mdns.Packet{
		Header: mdns.Header{QDCount: 1},
		Questions: []mdns.Question{
			mdns.Question{
				Domain:  fuchsiaService,
				Type:    mdns.PTR,
				Class:   mdns.IN,
				Unicast: false,
			},
		},
	}
	devices, err := cmd.sendMDNSPacket(ctx, listPacket)
	if err != nil {
		return nil, fmt.Errorf("sending/receiving mdns packets: %v", err)
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
	cmd.mdnsHandler = listMDNSHandler
	if err := cmd.execute(ctx); err != nil {
		log.Print(err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
