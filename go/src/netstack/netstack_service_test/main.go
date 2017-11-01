// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"syscall/zx"
	"syscall/zx/mxerror"
	"time"

	"github.com/google/netstack/tcpip"

	"app/context"
	"fidl/bindings"

	"garnet/public/lib/netstack/fidl/net_address"
	"garnet/public/lib/netstack/fidl/netstack"
)

type testApp struct {
	ctx      *context.Context
	netstack *netstack.Netstack_Proxy
}

func main() {
	a := &testApp{ctx: context.CreateFromStartupInfo()}

	var listen bool
	var getaddr string
	flag.BoolVar(&listen, "listen", false, "Listen for notifications and print interfaces every time they change")
	flag.StringVar(&getaddr, "getaddr", "", "Lookup the given address via DNS")
	flag.Parse()

	r, p := a.netstack.NewRequest(bindings.GetAsyncWaiter())
	a.netstack = p
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(r)

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
	port, _ := a.netstack.GetPortForService("http", netstack.Protocol_Tcp)
	resp, netErr, _ := a.netstack.GetAddress(name, port)
	if netErr.Status != netstack.Status_Ok {
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
	r, p := netstack.NewChannelForNotificationListener()
	s := r.NewStub(a, bindings.GetAsyncWaiter())
	a.netstack.RegisterListener(&p)

	go func() {
		for {
			if err := s.ServeRequest(); err != nil {
				if mxerror.Status(err) != zx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
	}()
	select {}
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
