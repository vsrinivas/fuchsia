// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"os"

	"application/lib/app/context"
	"fidl/bindings"

	"apps/netstack/services/netstack"
)

type NetstackClientApp struct {
	ctx      *context.Context
	netstack *netstack.Netstack_Proxy
}

func (a *NetstackClientApp) Start(name string) {
	r, p := a.netstack.NewRequest(bindings.GetAsyncWaiter())
	a.netstack = p
	a.ctx.ConnectToEnvService(r)

	fmt.Printf("looking up %v...\n", name)
	port, err := a.netstack.GetPortForService("http", netstack.Protocol_Tcp)
	resp, err := a.netstack.GetAddress(name, port)
	if err != nil {
		log.Println(err)
	} else {
		for _, addr := range resp {
			if addr.Addr.Ipv4 != nil {
				fmt.Printf("ipv4=%v\n", addr.Addr.Ipv4)
			} else {
				fmt.Printf("ipv6=%v\n", addr.Addr.Ipv6)
			}

		}
	}
	a.netstack.Close()

}

func main() {
	name := "google.com"
	if len(os.Args) > 1 {
		name = os.Args[1]
	}

	a := &NetstackClientApp{ctx: context.CreateFromStartupInfo()}
	a.Start(name)
}
