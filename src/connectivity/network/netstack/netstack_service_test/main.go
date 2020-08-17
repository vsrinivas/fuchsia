// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"
)

type testApp struct {
	ctx      *component.Context
	netstack *netstack.NetstackWithCtxInterface
}

func main() {
	a := &testApp{ctx: component.NewContextFromStartupInfo()}

	var listen bool
	flag.BoolVar(&listen, "listen", false, "Listen for notifications and print interfaces every time they change")
	flag.Parse()

	req, pxy, err := netstack.NewNetstackWithCtxInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	a.netstack = pxy
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(req)

	if listen {
		a.listen()
	}
}

func usage() {
	fmt.Printf("Usage: %s [OPTIONS]\n", os.Args[0])
	flag.PrintDefaults()
}

func (a *testApp) listen() {
	fmt.Printf("Listening for changes...\n")
	for {
		interfaces, err := a.netstack.ExpectOnInterfacesChanged(context.Background())
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
	fmt.Printf("%s: addr=%s [%s]\n", iface.Name, netAddrToString(iface.Addr), iface.Flags)
}

// TODO(tamird): this exact function exists in ifconfig.
func netAddrToString(addr net.IpAddress) string {
	return fidlconv.ToTCPIPAddress(addr).String()
}
