// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"net"
	"os"
	"strings"
	"syscall/zx"

	"app/context"
	"netstack/fidlconv"

	"fidl/fuchsia/hardware/ethernet"
	netfidl "fidl/fuchsia/net"
	"fidl/fuchsia/netstack"
	"fidl/fuchsia/wlan/service"

	"github.com/google/netstack/tcpip"
)

type netstackClientApp struct {
	ctx      *context.Context
	netstack *netstack.NetstackInterface
	wlan     *service.WlanInterface
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

func getIfaceByNameFromIfaces(name string, ifaces []netstack.NetInterface) *netstack.NetInterface {
	var candidate *netstack.NetInterface
	for i, iface := range ifaces {
		if strings.HasPrefix(iface.Name, name) {
			if candidate != nil {
				return nil
			}
			candidate = &ifaces[i]
		}
	}
	return candidate
}

func getIfaceByIdFromIfaces(id uint32, ifaces []netstack.NetInterface) *netstack.NetInterface {
	for _, iface := range ifaces {
		if iface.Id == id {
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

	fmt.Printf("%s\tHWaddr %s Id:%d\n", iface.Name, hwAddrToString(iface.Hwaddr), iface.Id)
	fmt.Printf("\tinet addr:%s  Bcast:%s  Mask:%s\n", netAddrToString(iface.Addr), netAddrToString(iface.Broadaddr), netAddrToString(iface.Netmask))
	for _, addr := range iface.Ipv6addrs {
		// TODO: scopes
		fmt.Printf("\tinet6 addr: %s/%d Scope:Link\n", netAddrToString(addr.Addr), addr.PrefixLen)
	}
	fmt.Printf("\t%s\n", flagsToString(iface.Flags))

	if isWLAN(iface.Features) {
		fmt.Printf("\tWLAN Status: %s\n", a.wlanStatus())
	}

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
	netAddr, prefixLen := validateCidr(os.Args[3])
	result, _ := a.netstack.SetInterfaceAddress(iface.Id, netAddr, prefixLen)
	if result.Status != netstack.StatusOk {
		fmt.Printf("Error setting interface address: %s\n", result.Message)
	}
}

func (a *netstackClientApp) removeIfaceAddress(iface netstack.NetInterface, cidr string) {
	netAddr, prefixLen := validateCidr(os.Args[3])
	result, _ := a.netstack.RemoveInterfaceAddress(iface.Id, netAddr, prefixLen)
	if result.Status != netstack.StatusOk {
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
		in.Gateway = toIpAddress(net.ParseIP(val))
	case "iface":
		ifaces, err := a.netstack.GetInterfaces()
		if err != nil {
			return remaining, err
		}
		iface := getIfaceByNameFromIfaces(val, ifaces)
		if err != nil {
			return remaining, err
		}
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

	route.Destination = toIpAddress(dstAddr)
	route.Netmask = toIpAddress(net.IP(dstSubnet.Mask))

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
	if (r.Gateway == netfidl.IpAddress{}) && r.Nicid == 0 {
		return fmt.Errorf("either gateway or iface must be provided when adding a route")
	}
	req, transactionInterface, err := netstack.NewRouteTableTransactionInterfaceRequest()
	if err != nil {
		return fmt.Errorf("could not make a new route table transaction: %s", err)
	}
	defer req.Close()
	status, err := a.netstack.StartRouteTableTransaction(req)
	if err != nil || zx.Status(status) != zx.ErrOk {
		return fmt.Errorf("could not start a route table transaction: %s (%s)", err, zx.Status(status))
	}
	rs, err := transactionInterface.GetRouteTable()
	if err != nil {
		return fmt.Errorf("could not get route table from netstack: %s", err)
	}
	err = transactionInterface.SetRouteTable(append(rs, r))
	if err != nil {
		return fmt.Errorf("could not set route table in netstack: %s", err)
	}
	status, err = transactionInterface.Commit()
	if err != nil || zx.Status(status) != zx.ErrOk {
		return fmt.Errorf("could not commit route table in netstack: %s (%s)", err, zx.Status(status))
	}
	return nil
}

func equalIpAddress(a netfidl.IpAddress, b netfidl.IpAddress) bool {
	if a.Which() != b.Which() {
		return false
	}
	switch a.Which() {
	case netfidl.IpAddressIpv4:
		return a.Ipv4.Addr == b.Ipv4.Addr
	case netfidl.IpAddressIpv6:
		return a.Ipv6.Addr == b.Ipv6.Addr
	default:
		return false
	}
}

// Returns true if the target matches the source.  If the target is
// missing the gateway or nicid then those are considered to be
// matching.
func matchRoute(target netstack.RouteTableEntry, source netstack.RouteTableEntry) bool {
	if !equalIpAddress(target.Destination, source.Destination) {
		return false
	}
	if !equalIpAddress(target.Netmask, source.Netmask) {
		return false
	}
	if target.Gateway.Which() != 0 &&
		!equalIpAddress(source.Gateway, target.Gateway) {
		// The gateway is neither wildcard nor a match.
		return false
	}
	if target.Nicid != 0 && source.Nicid != target.Nicid {
		// The Nicid is neither wildcard nor a match.
		return false
	}
	return true
}

func (a *netstackClientApp) deleteRoute(target netstack.RouteTableEntry) error {
	req, transactionInterface, err := netstack.NewRouteTableTransactionInterfaceRequest()
	if err != nil {
		return fmt.Errorf("could not make a new route table transaction: %s", err)
	}
	defer req.Close()
	status, err := a.netstack.StartRouteTableTransaction(req)
	if err != nil || zx.Status(status) != zx.ErrOk {
		return fmt.Errorf("could not start a route table transaction (maybe the route table is locked?): %s", err)
	}

	rs, err := transactionInterface.GetRouteTable()
	if err != nil {
		return fmt.Errorf("could not get route table from netstack: %s", err)
	}
	for i, r := range rs {
		if matchRoute(target, r) {
			transactionInterface.SetRouteTable(append(rs[:i], rs[i+1:]...))
			_, err = transactionInterface.Commit()
			if err != nil {
				return fmt.Errorf("could not commit route table in netstack: %s", err)
			}
			return nil
		}
	}
	return fmt.Errorf("could not find route to delete in route table")
}

func routeTableEntryToString(r netstack.RouteTableEntry, ifaces []netstack.NetInterface) string {
	iface := getIfaceByIdFromIfaces(r.Nicid, ifaces)
	var ifaceName string
	if iface == nil {
		ifaceName = fmt.Sprintf("Nicid:%d", r.Nicid)
	} else {
		ifaceName = iface.Name
	}
	var netAndMask net.IPNet
	switch r.Destination.Which() {
	case netfidl.IpAddressIpv4:
		netAndMask = net.IPNet{IP: r.Destination.Ipv4.Addr[:], Mask: r.Netmask.Ipv4.Addr[:]}
	case netfidl.IpAddressIpv6:
		netAndMask = net.IPNet{IP: r.Destination.Ipv6.Addr[:], Mask: r.Netmask.Ipv6.Addr[:]}
	}
	return fmt.Sprintf("%s via %s %s", netAndMask.String(), netAddrToString(r.Gateway), ifaceName)
}

func (a *netstackClientApp) showRoutes() error {
	rs, err := a.netstack.GetRouteTable()
	if err != nil {
		return fmt.Errorf("Could not get route table from netstack: %s", err)
	}
	ifaces, err := a.netstack.GetInterfaces()
	if err != nil {
		return err
	}
	for _, r := range rs {
		fmt.Printf("%s\n", routeTableEntryToString(r, ifaces))
	}
	return nil
}

func (a *netstackClientApp) bridge(ifNames []string) error {
	ifs := make([]*netstack.NetInterface, len(ifNames))
	nicIDs := make([]uint32, len(ifNames))
	// first, validate that all interfaces exist
	ifaces, err := a.netstack.GetInterfaces()
	if err != nil {
		return err
	}
	for i, ifName := range ifNames {
		iface := getIfaceByNameFromIfaces(ifName, ifaces)
		if iface == nil {
			return fmt.Errorf("no such interface '%s'\n", ifName)
		}
		ifs[i] = iface
		nicIDs[i] = iface.Id
	}

	result, _ := a.netstack.BridgeInterfaces(nicIDs)
	if result.Status != netstack.StatusOk {
		return fmt.Errorf("error bridging interfaces: %s, result: %s", ifNames, result)
	}

	return nil
}

func (a *netstackClientApp) setDHCP(iface netstack.NetInterface, startStop string) {
	switch startStop {
	case "start":
		a.netstack.SetDhcpClientStatus(iface.Id, true)
	case "stop":
		a.netstack.SetDhcpClientStatus(iface.Id, false)
	default:
		usage()
	}
}

func (a *netstackClientApp) wlanStatus() string {
	if a.wlan == nil {
		return "failed to query (FIDL service unintialized)"
	}
	res, err := a.wlan.Status()
	if err != nil {
		return fmt.Sprintf("failed to query (error: %v)", err)
	} else if res.Error.Code != service.ErrCodeOk {
		return fmt.Sprintf("failed to query (err: code(%v) desc(%v)", res.Error.Code, res.Error.Description)
	} else {
		status := wlanStateToStr(res.State)
		if res.CurrentAp != nil {
			ap := res.CurrentAp
			isSecureStr := ""
			if ap.IsSecure {
				isSecureStr = "*"
			}
			status += fmt.Sprintf(" BSSID: %x SSID: %q Security: %v RSSI: %d",
				ap.Bssid, ap.Ssid, isSecureStr, ap.RssiDbm)
		}
		return status
	}
}

func wlanStateToStr(state service.State) string {
	switch state {
	case service.StateBss:
		return "starting-bss"
	case service.StateQuerying:
		return "querying"
	case service.StateScanning:
		return "scanning"
	case service.StateJoining:
		return "joining"
	case service.StateAuthenticating:
		return "authenticating"
	case service.StateAssociating:
		return "associating"
	case service.StateAssociated:
		return "associated"
	default:
		return "unknown"
	}

}

func hwAddrToString(hwaddr []uint8) string {
	var b strings.Builder
	for i, d := range hwaddr {
		if i > 0 {
			b.WriteByte(':')
		}
		fmt.Fprintf(&b, "%02x", d)
	}
	return b.String()
}

func netAddrToString(addr netfidl.IpAddress) string {
	return fidlconv.ToTCPIPAddress(addr).String()
}

func flagsToString(flags uint32) string {
	var b strings.Builder
	if flags&netstack.NetInterfaceFlagUp != 0 {
		b.WriteString("UP")
	}
	return b.String()
}

func toIpAddress(addr net.IP) netfidl.IpAddress {
	// TODO(eyalsoha): this doesn't correctly handle IPv6-mapped IPv4 addresses.
	if ipv4 := addr.To4(); ipv4 != nil {
		addr = ipv4
	}
	return fidlconv.ToNetIpAddress(tcpip.Address(addr))
}

func validateCidr(cidr string) (address netfidl.IpAddress, prefixLength uint8) {
	netAddr, netSubnet, err := net.ParseCIDR(cidr)
	if err != nil {
		fmt.Printf("Error parsing CIDR notation: %s, error: %v\n", cidr, err)
		usage()
	}
	prefixLen, _ := netSubnet.Mask.Size()

	return toIpAddress(netAddr), uint8(prefixLen)
}

func isWLAN(features uint32) bool {
	return features&ethernet.InfoFeatureWlan != 0
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
	fmt.Printf("  %s [<interface>] dhcp [start|stop]\n", os.Args[0])
	fmt.Printf("  %s route [add|del] [<address>/<mask>] [iface <name>] [gateway <address>/<mask>]\n", os.Args[0])
	fmt.Printf("  %s route show\n", os.Args[0])
	fmt.Printf("  %s bridge [<interface>]+\n", os.Args[0])
	os.Exit(1)
}

func main() {
	a := &netstackClientApp{ctx: context.CreateFromStartupInfo()}
	req, pxy, err := netstack.NewNetstackInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	a.netstack = pxy
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(req)

	reqWlan, pxyWlan, errWlan := service.NewWlanInterfaceRequest()
	if errWlan == nil {
		a.wlan = pxyWlan
		defer a.wlan.Close()
		a.ctx.ConnectToEnvService(reqWlan)
	}

	if len(os.Args) == 1 {
		a.printAll()
		return
	}

	var iface *netstack.NetInterface
	switch os.Args[1] {
	case "route":
		if len(os.Args) == 2 {
			fmt.Printf("Missing route operation.\n")
			usage()
		}
		op := os.Args[2]
		if op == "show" {
			err = a.showRoutes()
			if err != nil {
				fmt.Printf("Error showing routes: %s\n", err)
			}
			return
		}
		if len(os.Args) < 4 {
			fmt.Printf("Not enough arguments to `ifconfig route`; at least a destination and one of iface name or gateway must be provided\n")
			usage()
		}
		routeFlags := os.Args[3:]
		r, err := a.newRouteFromArgs(routeFlags)
		if err != nil {
			fmt.Printf("Error parsing route from args: %s, error: %s\n", routeFlags, err)
			usage()
		}

		switch op {
		case "add":
			if err = a.addRoute(r); err != nil {
				fmt.Printf("Error adding route to route table: %s", err)
				usage()
			}
		case "del":
			err = a.deleteRoute(r)
			if err != nil {
				fmt.Printf("Error deleting route from route table: %s", err)
				usage()
			}
		default:
			fmt.Printf("Unknown route operation: %s", op)
			usage()
		}

		return
	case "bridge":
		ifaces := os.Args[2:]
		err := a.bridge(ifaces)
		if err != nil {
			fmt.Printf("error creating bridge: %s", err)
		}
		// fmt.Printf("Created virtual nic %s", bridge)
		fmt.Printf("Bridged interfaces %s\n", ifaces)
		return
	case "help":
		usage()
		return
	default:
		ifaces, err := a.netstack.GetInterfaces()
		if err != nil {
			fmt.Printf("Error finding interface name: %s\n", err)
			return
		}
		iface = getIfaceByNameFromIfaces(os.Args[1], ifaces)
		if err != nil {
			fmt.Printf("Error finding interface name: %s\n", err)
			return
		}
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
			a.removeIfaceAddress(*iface, os.Args[3])
		case "dhcp":
			a.setDHCP(*iface, os.Args[3])
		default:
			usage()
		}

	default:
		usage()
	}
}
