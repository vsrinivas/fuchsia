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

type icmpHistogram struct {
	// see third_party/netstack/tcpip/header/icmpv{4,6}.go for an exhaustive list
	echoRequests uint64
	echoReplies  uint64
}

type icmpOutput struct {
	received        uint64
	inputFailed     uint64
	sent            uint64
	sentFailed      uint64
	inputHistogram  icmpHistogram
	outputHistogram icmpHistogram
}

type statsOutput struct {
	icmp icmpOutput
}

func (h icmpHistogram) String() string {
	return fmt.Sprintf("\t\techo request: %d\n\t\techo replies: %d", h.echoRequests, h.echoReplies)
}

func (o *statsOutput) String() string {
	return fmt.Sprintf(
		`Icmp:
      %d ICMP messages received
      %d input ICMP message failed.
      ICMP input histogram:
      %v
      %d ICMP messages sent
      %d ICMP messages failed
      ICMP output histogram:
      %v`,
		o.icmp.received,
		o.icmp.inputFailed,
		o.icmp.inputHistogram,
		o.icmp.sent,
		o.icmp.sentFailed,
		o.icmp.outputHistogram)
}

func (o *statsOutput) add(stats netstack.NetInterfaceStats) {
	tx := stats.Tx
	rx := stats.Rx

	o.icmp.sent += tx.PktsEchoReq + tx.PktsEchoReqV6 + tx.PktsEchoRep + tx.PktsEchoRepV6
	o.icmp.received += rx.PktsEchoReq + rx.PktsEchoReqV6 + rx.PktsEchoRep + rx.PktsEchoRepV6
	o.icmp.outputHistogram.echoRequests += tx.PktsEchoReq + tx.PktsEchoReqV6
	o.icmp.outputHistogram.echoReplies += tx.PktsEchoRep + tx.PktsEchoRepV6
	o.icmp.inputHistogram.echoRequests += rx.PktsEchoReq + rx.PktsEchoReqV6
	o.icmp.inputHistogram.echoReplies += rx.PktsEchoRep + rx.PktsEchoRepV6
}

func dumpStats(a *netstatApp) {
	nics, err := a.netstack.GetInterfaces()
	if err != nil {
		errorf("Failed to get interfaces: %v\n.", err)
		return
	}

	stats := &statsOutput{}
	for _, nic := range nics {
		nicStats, err := a.netstack.GetStats(nic.Id)
		if err != nil {
			errorf("Failed to get statistics for nic: %v\n", err)
		} else {
			stats.add(nicStats)
		}
	}

	fmt.Printf("%v\n", stats)
}

func dumpRouteTables(a *netstatApp) {
	entries, err := a.netstack.GetRouteTable()
	if err != nil {
		errorf("Failed to get route table: %v\n", err)
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

var hasErrors bool = false

func errorf(format string, args ...interface{}) {
	hasErrors = true
	fmt.Printf("netstat: "+format, args...)
}

func main() {
	a := &netstatApp{ctx: context.CreateFromStartupInfo()}

	flag.Usage = usage

	var showRouteTables bool
	var showStats bool
	flag.BoolVar(&showRouteTables, "r", false, "Dump the Route Tables")
	flag.BoolVar(&showStats, "s", false, "Show network statistics")
	flag.Parse()

	r, p := a.netstack.NewRequest(bindings.GetAsyncWaiter())
	a.netstack = p
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(r)

	if showRouteTables {
		dumpRouteTables(a)
	}
	if showStats {
		dumpStats(a)
	}

	if hasErrors {
		os.Exit(1)
	}
}
