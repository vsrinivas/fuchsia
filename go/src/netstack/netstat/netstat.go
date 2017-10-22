// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/google/netstack/tcpip"

	"app/context"
	"fidl/bindings"

	"garnet/public/lib/netstack/fidl/net_address"
	"garnet/public/lib/netstack/fidl/netstack"
)

type netstatApp struct {
	ctx      *context.Context
	netstack *netstack.Netstack_Proxy
}

func dumpRouteTables(a *netstatApp) {
	entries, err := a.netstack.GetRouteTable()
	if err != nil {
		fmt.Printf("Failed to fetch routing Table: %v\n", err)
		return
	}
	for _, entry := range entries {
		if netAddressZero(entry.Destination) {
			fmt.Printf("default via %s, ", netAddressToString(entry.Gateway))
		} else {
			fmt.Printf("Destination: %s, ", netAddressToString(entry.Destination))
			fmt.Printf("Mask: %s, ", netAddressToString(entry.Netmask))
			if !netAddressZero(entry.Gateway) {
				fmt.Printf("Gateway: %s, ", netAddressToString(entry.Gateway))
			}
		}
		fmt.Printf("NICID: %d\n", entry.Nicid)
	}
}

func netAddressZero(addr net_address.NetAddress) bool {
	switch addr.Family {
	case net_address.NetAddressFamily_Ipv4:
		for _, b := range addr.Ipv4 {
			if b != 0 {
				return false
			}
		}
		return true
	case net_address.NetAddressFamily_Ipv6:
		for _, b := range addr.Ipv6 {
			if b != 0 {
				return false
			}
		}
		return true
	}
	return true
}

func netAddressToString(addr net_address.NetAddress) string {
	switch addr.Family {
	case net_address.NetAddressFamily_Ipv4:
		a := tcpip.Address(addr.Ipv4[:])
		return fmt.Sprintf("%s", a)
	case net_address.NetAddressFamily_Ipv6:
		a := tcpip.Address(addr.Ipv6[:])
		return fmt.Sprintf("%s", a)
	}
	return ""
}

func usage() {
	fmt.Printf("Usage: %s [OPTIONS]\n", os.Args[0])
	flag.PrintDefaults()
}

func main() {
	a := &netstatApp{ctx: context.CreateFromStartupInfo()}

	flag.Usage = usage

	var showRouteTables bool
	flag.BoolVar(&showRouteTables, "r", false, "Dump the Route Tables")
	flag.Parse()

	r, p := a.netstack.NewRequest(bindings.GetAsyncWaiter())
	a.netstack = p
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(r)

	if showRouteTables {
		dumpRouteTables(a)
	}
}
