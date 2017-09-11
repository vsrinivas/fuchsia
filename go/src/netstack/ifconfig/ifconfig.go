// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"

	"github.com/google/netstack/tcpip"

	"app/context"
	"fidl/bindings"

	"garnet/public/lib/netstack/fidl/net_address"
	"garnet/public/lib/netstack/fidl/netstack"
)

type netstackClientApp struct {
	ctx      *context.Context
	netstack *netstack.Netstack_Proxy
}

func (a *netstackClientApp) start() {
	r, p := a.netstack.NewRequest(bindings.GetAsyncWaiter())
	a.netstack = p
	a.ctx.ConnectToEnvService(r)

	ifaces, err := a.netstack.GetInterfaces()
	if err != nil {
		fmt.Print("Failed to fetch interfaces.\n")
		return
	}

	for _, iface := range ifaces {
		stats, err := a.netstack.GetStats(iface.Id)
		if err != nil {
			fmt.Printf("Failed to lookup %v.\n\n", iface.Name)
			continue
		}
		printIface(iface, stats)
		fmt.Print("\n")
	}

	a.netstack.Close()
}

func printIface(iface netstack.NetInterface, stats netstack.NetInterfaceStats) {
	fmt.Printf("%s\tHWaddr %s\n", iface.Name, hwAddrToString(iface.Hwaddr))
	fmt.Printf("\tinet addr:%s  Bcast:%s  Mask:%s\n", netAddrToString(iface.Addr), netAddrToString(iface.Broadaddr), netAddrToString(iface.Netmask))
	for _, addr := range iface.Ipv6addrs {
		// TODO: scopes
		fmt.Printf("\tinet6 addr: %s/%d Scope:Link\n", netAddrToString(addr.Addr), addr.PrefixLen)
	}
	fmt.Printf("\t%s\n", flagsToString(iface.Flags))

	fmt.Printf("\tRX packets:%d\n", stats.RxPktsTotal)
	fmt.Printf("\tTX packets:%d\n", stats.TxPktsTotal)
	fmt.Printf("\tRX bytes:%s  TX bytes:%s\n",
		bytesToString(stats.RxBytesTotal), bytesToString(stats.TxBytesTotal))
	// TODO: more stats. MTU, RX/TX errors
}

func hwAddrToString(hwaddr []uint8) string {
	str := ""
	for i := 0; i < len(hwaddr); i++ {
		if i > 0 {
			str += ":"
		}
		str += fmt.Sprintf("%x", hwaddr[i])
	}
	return str
}

func netAddrToString(addr net_address.NetAddress) string {
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

func flagsToString(flags uint32) string {
	str := ""
	if flags&netstack.NetInterfaceFlagUp != 0 {
		str += "UP"
	}
	return str
}

// bytesToString returns a human-friendly display of the given byte count.
// Values over 1K have an approximation appended in parentheses.
// bytesToString(42) => "42"
// bytesToString(42*1024 + 200) => "43208 (42.2 KB)"
func bytesToString(bytes uint64) string {
	if bytes == 0 {
		return "0"
	}

	units := "BKMGT"
	unitSize := uint64(1)
	unit := "B"

	for i := 0; i+1 < len(units) && bytes >= unitSize*1024; i++ {
		unitSize *= 1024
		unit = string(units[i+1]) + "B"
	}

	if unitSize == 1 {
		return fmt.Sprintf("%d", bytes)
	} else {
		value := float32(bytes) / float32(unitSize)
		return fmt.Sprintf("%d (%.1f %s)", bytes, value, unit)
	}
}

func main() {
	a := &netstackClientApp{ctx: context.CreateFromStartupInfo()}
	a.start()
}
