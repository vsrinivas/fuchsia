// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"strings"

	"github.com/google/subcommands"

	"fuchsia.googlesource.com/tools/mdns"
)

const (
	fuchsiaService = "_fuchsia._udp.local"
)

// jsonListOutput represents the output in JSON format.
type jsonListOutput struct {
	// List of devices found.
	Devices []jsonListDevice `json:"devices"`
}

type jsonListDevice struct {
	// Device IP address.
	Addr string `json:"addr"`
	// Device domain name. Only output with -full.
	Domain string `json:"domain,omitempty"`
}

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
			// This is a bit convoluted: the domain param is being used
			// as a "service", and the Data field actually contains the
			// domain of the device.
			nameLength := int(a.Data[0])
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

func (cmd *listCmd) execute(ctx context.Context) error {
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
		return fmt.Errorf("sending/receiving mdns packets: %v", err)
	}
	filteredDevices := make([]*fuchsiaDevice, 0)
	for _, device := range devices {
		if strings.Contains(device.domain, cmd.domainFilter) {
			filteredDevices = append(filteredDevices, device)
		}
	}
	if len(filteredDevices) == 0 {
		return fmt.Errorf("no devices with domain matching '%v'", cmd.domainFilter)
	}

	if cmd.json {
		return cmd.outputJSON(filteredDevices)
	}
	return cmd.outputNormal(filteredDevices)
}

func (cmd *listCmd) outputNormal(filteredDevices []*fuchsiaDevice) error {
	for _, device := range filteredDevices {
		if cmd.fullInfo {
			fmt.Printf("%v %v\n", device.addr, device.domain)
		} else {
			fmt.Printf("%v\n", device.addr)
		}
	}
	return nil
}

func (cmd *listCmd) outputJSON(filteredDevices []*fuchsiaDevice) error {
	jsonOut := jsonListOutput{Devices: make([]jsonListDevice, 0, len(filteredDevices))}

	for _, device := range filteredDevices {
		dev := jsonListDevice{Addr: device.addr.String()}
		if cmd.fullInfo {
			dev.Domain = device.domain
		}
		jsonOut.Devices = append(jsonOut.Devices, dev)
	}

	e := json.NewEncoder(os.Stdout)
	e.SetIndent("", "  ")
	return e.Encode(jsonOut)
}

func (cmd *listCmd) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	cmd.mdnsHandler = listMDNSHandler
	if err := cmd.execute(ctx); err != nil {
		log.Print(err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
