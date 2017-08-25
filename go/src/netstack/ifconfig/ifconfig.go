// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"

	"github.com/google/netstack/tcpip"

	"application/lib/app/context"
	"fidl/bindings"

	"apps/netstack/services/net_address"
	"apps/netstack/services/netstack"
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
		printIface(iface)
		fmt.Print("\n")
	}

	a.netstack.Close()
}

func printIface(iface netstack.NetInterface) {
	fmt.Printf("%s\tHWaddr %s\n", iface.Name, hwAddrToString(iface.Hwaddr))
	fmt.Printf("\tinet addr:%s  Bcast:%s  Mask:%s\n", netAddrToString(iface.Addr), netAddrToString(iface.Broadaddr), netAddrToString(iface.Netmask))
	for _, addr := range iface.Ipv6addrs {
		// TODO: scopes
		fmt.Printf("\tinet6 addr: %s/%d Scope:Link\n", netAddrToString(addr.Addr), addr.PrefixLen)
	}
	fmt.Printf("\t%s\n", flagsToString(iface.Flags))
	// TODO: more stats. MTU, RX/TX packets/errors/bytes
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

func main() {
	a := &netstackClientApp{ctx: context.CreateFromStartupInfo()}
	a.start()
}
