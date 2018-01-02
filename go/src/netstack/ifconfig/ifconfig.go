// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"net"
	"os"

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

func (a *netstackClientApp) printAll() {
	ifaces, err := a.netstack.GetInterfaces()
	if err != nil {
		fmt.Print("ifconfig: failed to fetch interfaces\n")
		return
	}

	for _, iface := range ifaces {
		a.printIface(iface)
	}
}

func (a *netstackClientApp) getIfaceByName(name string) *netstack.NetInterface {
	ifaces, err := a.netstack.GetInterfaces()
	if err != nil {
		fmt.Print("ifconfig: failed to fetch interfaces\n")
		return nil
	}

	for _, iface := range ifaces {
		if iface.Name == name {
			return &iface
		}
	}
	return nil
}

func (a *netstackClientApp) printIface(iface netstack.NetInterface) {
	stats, err := a.netstack.GetStats(iface.Id)
	if err != nil {
		fmt.Printf("ifconfig: failed to fetch stats for '%v'\n\n", iface.Name)
		return
	}

	fmt.Printf("%s\tHWaddr %s\n", iface.Name, hwAddrToString(iface.Hwaddr))
	fmt.Printf("\tinet addr:%s  Bcast:%s  Mask:%s\n", netAddrToString(iface.Addr), netAddrToString(iface.Broadaddr), netAddrToString(iface.Netmask))
	for _, addr := range iface.Ipv6addrs {
		// TODO: scopes
		fmt.Printf("\tinet6 addr: %s/%d Scope:Link\n", netAddrToString(addr.Addr), addr.PrefixLen)
	}
	fmt.Printf("\t%s\n", flagsToString(iface.Flags))

	fmt.Printf("\tRX packets:%d\n", stats.Rx.PktsTotal)
	fmt.Printf("\tTX packets:%d\n", stats.Tx.PktsTotal)
	fmt.Printf("\tRX bytes:%s  TX bytes:%s\n",
		bytesToString(stats.Rx.BytesTotal), bytesToString(stats.Tx.BytesTotal))
	fmt.Printf("\n")
	// TODO: more stats. MTU, RX/TX errors
}

func (a *netstackClientApp) setStatus(iface netstack.NetInterface, up bool) {
	a.netstack.SetInterfaceStatus(iface.Id, up)
}

func (a *netstackClientApp) addIfaceAddress(iface netstack.NetInterface, cidr string) {
	netAddr, netSubnet, err := net.ParseCIDR(os.Args[3])
	if err != nil {
		fmt.Printf("Error parsing CIDR notation: %s, error: %v\n", os.Args[3], err)
		usage()
	}
	prefixLen, _ := netSubnet.Mask.Size()
	result, _ := a.netstack.SetInterfaceAddress(iface.Id, toNetAddress(netAddr), uint64(prefixLen))
	if result.Status != netstack.Status_Ok {
		fmt.Printf("Error setting interface address: %s\n", result.Message)
	}
}

func (a *netstackClientApp) parseRouteAttribute(in *netstack.RouteTableEntry, args []string) (remaining []string, err error) {
	if len(args) < 2 {
		return args, fmt.Errorf("not enough args to make attribute")
	}
	var attr, val string
	switch attr, val, remaining = args[0], args[1], args[2:]; attr {
	case "gateway":
		in.Gateway = toNetAddress(net.ParseIP(val))
	case "iface":
		iface := a.getIfaceByName(val)
		if iface == nil {
			return remaining, fmt.Errorf("no such interface '%s'\n", val)
		}

		in.Nicid = iface.Id
	default:
		return remaining, fmt.Errorf("unknown route attribute: %s %s", attr, val)
	}

	return remaining, nil
}

func (a *netstackClientApp) newRouteFromArgs(args []string) (route netstack.RouteTableEntry, err error) {
	destination, remaining := args[0], args[1:]
	dstAddr, dstSubnet, err := net.ParseCIDR(destination)
	if err != nil {
		return route, fmt.Errorf("invalid destination (destination must be provided in CIDR format): %s", destination)
	}

	route.Destination = toNetAddress(dstAddr)
	route.Netmask = toNetAddress(net.IP(dstSubnet.Mask))

	for len(remaining) > 0 {
		remaining, err = a.parseRouteAttribute(&route, remaining)
		if err != nil {
			return route, err
		}
	}

	if len(remaining) != 0 {
		return route, fmt.Errorf("could not parse all route arguments. remaining: %s", remaining)
	}

	return route, nil
}

func (a *netstackClientApp) addRoute(r netstack.RouteTableEntry) error {
	rs, err := a.netstack.GetRouteTable()
	if err != nil {
		return fmt.Errorf("Could not get route table from netstack: %s", err)
	}

	return a.netstack.SetRouteTable(append(rs, r))
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

func isIPv4(ip net.IP) bool {
	return ip.DefaultMask() != nil
}

func toNetAddress(addr net.IP) net_address.NetAddress {
	out := net_address.NetAddress{Family: net_address.NetAddressFamily_Unspecified}
	if isIPv4(addr) {
		out.Family = net_address.NetAddressFamily_Ipv4
		out.Ipv4 = &[4]uint8{}
		copy(out.Ipv4[:], addr[len(addr)-4:])
	} else {
		out.Family = net_address.NetAddressFamily_Ipv6
		out.Ipv6 = &[16]uint8{}
		copy(out.Ipv6[:], addr[:])
	}
	return out
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

func usage() {
	fmt.Printf("Usage:\n")
	fmt.Printf("  %s [<interface>]\n", os.Args[0])
	fmt.Printf("  %s [<interface>] [up|down]\n", os.Args[0])
	fmt.Printf("  %s [<interface>] [add|del] [<address>]/[<mask>]\n", os.Args[0])
}

func main() {
	a := &netstackClientApp{ctx: context.CreateFromStartupInfo()}
	r, p := a.netstack.NewRequest(bindings.GetAsyncWaiter())
	a.netstack = p
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(r)

	if len(os.Args) == 1 {
		a.printAll()
		return
	}

	var iface *netstack.NetInterface
	switch os.Args[1] {
	case "route":
		routeFlags := os.Args[3:]
		r, err := a.newRouteFromArgs(routeFlags)
		if err != nil {
			fmt.Printf("Error parsing route from args: %s, error: %s\n", routeFlags, err)
		}

		switch op := os.Args[2]; op {
		case "add":
			err = a.addRoute(r)
			if err != nil {
				fmt.Printf("Error adding route to route table: %s", err)
			}
		case "del":
			fmt.Printf("Deleting routes from the route table is not yet supported.\n")
		default:
			fmt.Printf("Unknown route operation: %s", op)
			usage()
		}

		return
	default:
		iface = a.getIfaceByName(os.Args[1])
		if iface == nil {
			fmt.Printf("ifconfig: no such interface '%s'\n", os.Args[1])
			return
		}
	}

	switch len(os.Args) {
	case 2:
		a.printIface(*iface)
	case 3, 4:
		switch os.Args[2] {
		case "up":
			a.setStatus(*iface, true)
		case "down":
			a.setStatus(*iface, false)
		case "add":
			a.addIfaceAddress(*iface, os.Args[3])
		case "del":
			fmt.Printf("Deleting addresses from an interface is not yet supported.\n")
			usage()
		default:
			usage()
		}

	default:
		usage()
	}
}
