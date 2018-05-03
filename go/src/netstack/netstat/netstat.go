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

	"fidl/netstack"
)

type netstatApp struct {
	ctx      *context.Context
	netstack *netstack.NetstackInterface
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
	ip   netstack.IpStats
	tcp  netstack.TcpStats
	udp  netstack.UdpStats
}

func (h icmpHistogram) String() string {
	return fmt.Sprintf("\t\techo request: %d\n\t\techo replies: %d", h.echoRequests, h.echoReplies)
}

func (o *statsOutput) String() string {
	return fmt.Sprintf(
		`IP:
	%d total packets received
	%d with invalid addresses
	%d incoming packets discarded
	%d incoming packets delivered
	%d requests sent out
	%d outgoing packets with errors
TCP:
	%d ActiveConnectionOpenings
	%d PassiveConnectionOpenings
	%d FailedConnectionAttempts
	%d ValidSegmentsReceived
	%d InvalidSegmentsReceived
	%d SegmentsSent
	%d ResetsSent
UDP:
	%d packets received
	%d packet receive errors
	%d packets to unknown ports received
	%d receive buffer errors
	%d malformed packets received
	%d packets sent
ICMP:
	%d ICMP messages received
	%d input ICMP message failed.
	ICMP input histogram:
%v
	%d ICMP messages sent
	%d ICMP messages failed
	ICMP output histogram:
%v`,
		o.ip.PacketsReceived,
		o.ip.InvalidAddressesReceived,
		o.ip.PacketsDiscarded,
		o.ip.PacketsDelivered,
		o.ip.PacketsSent,
		o.ip.OutgoingPacketErrors,
		o.tcp.ActiveConnectionOpenings,
		o.tcp.PassiveConnectionOpenings,
		o.tcp.FailedConnectionAttempts,
		o.tcp.ValidSegmentsReceived,
		o.tcp.InvalidSegmentsReceived,
		o.tcp.SegmentsSent,
		o.tcp.ResetsSent,
		o.udp.PacketsReceived,
		o.udp.UnknownPortErrors+o.udp.ReceiveBufferErrors+o.udp.MalformedPacketsReceived,
		o.udp.UnknownPortErrors,
		o.udp.ReceiveBufferErrors,
		o.udp.MalformedPacketsReceived,
		o.udp.PacketsSent,
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
	as, _ := a.netstack.GetAggregateStats()
	stats.ip = as.IpStats
	stats.tcp = as.TcpStats
	stats.udp = as.UdpStats
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

func netAddressZero(addr netstack.NetAddress) bool {
	switch addr.Family {
	case netstack.NetAddressFamilyIpv4:
		for _, b := range addr.Ipv4.Addr {
			if b != 0 {
				return false
			}
		}
		return true
	case netstack.NetAddressFamilyIpv6:
		for _, b := range addr.Ipv6.Addr {
			if b != 0 {
				return false
			}
		}
		return true
	}
	return true
}

func netAddressToString(addr netstack.NetAddress) string {
	switch addr.Family {
	case netstack.NetAddressFamilyIpv4:
		a := tcpip.Address(addr.Ipv4.Addr[:])
		return fmt.Sprintf("%s", a)
	case netstack.NetAddressFamilyIpv6:
		a := tcpip.Address(addr.Ipv6.Addr[:])
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

	req, pxy, err := netstack.NewNetstackInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	a.netstack = pxy
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(req)

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
