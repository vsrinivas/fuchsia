// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"time"

	"github.com/google/netstack/tcpip"

	"app/context"

	"fidl/fuchsia/netstack"
)

type testApp struct {
	ctx      *context.Context
	netstack *netstack.NetstackInterface
}

func main() {
	a := &testApp{ctx: context.CreateFromStartupInfo()}

	var listen bool
	var getaddr string
	flag.BoolVar(&listen, "listen", false, "Listen for notifications and print interfaces every time they change")
	flag.StringVar(&getaddr, "getaddr", "", "Lookup the given address via DNS")
	flag.Parse()

	req, pxy, err := netstack.NewNetstackInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	a.netstack = pxy
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(req)

	if getaddr != "" {
		a.getAddr(getaddr)
	}
	if listen {
		a.listen()
	}
}

func usage() {
	fmt.Printf("Usage: %s [OPTIONS]\n", os.Args[0])
	flag.PrintDefaults()
}

func (a *testApp) getAddr(name string) {
	fmt.Printf("Looking up %v... ", name)
	port, _ := a.netstack.GetPortForService("http", netstack.ProtocolTcp)
	resp, netErr, _ := a.netstack.GetAddress(name, port)
	if netErr.Status != netstack.StatusOk {
		log.Printf("failed: %v\n", netErr)
	} else {
		fmt.Printf("%v entries found\n", len(resp))
		for _, addr := range resp {
			fmt.Printf("%v\n", netAddrToString(addr.Addr))
		}
	}
}

func (a *testApp) listen() {
	fmt.Printf("Listening for changes...\n")
	for {
		interfaces, err := a.netstack.ExpectOnInterfacesChanged()
		if err != nil {
			fmt.Println("OnInterfacesChanged failed:", err)
		}
		a.OnInterfacesChanged(interfaces)
	}
}

// Implements netstack.NotificationListener.
func (a *testApp) OnInterfacesChanged(ifaces []netstack.NetInterface) error {
	fmt.Printf("--- Interfaces changed: %v\n", time.Now())
	for _, iface := range ifaces {
		printIface(iface)
	}
	return nil
}

func printIface(iface netstack.NetInterface) {
	fmt.Printf("%s: addr=%s [%s]\n", iface.Name, netAddrToString(iface.Addr), flagsToString(iface.Flags))
}

func netAddrToString(addr netstack.NetAddress) string {
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

func flagsToString(flags uint32) string {
	str := ""
	if flags&netstack.NetInterfaceFlagUp != 0 {
		str += "UP"
	}
	return str
}
