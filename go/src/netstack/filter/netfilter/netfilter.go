// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"os"

	"app/context"

	"fidl/fuchsia/netstack"
)

const commandName = "netfilter"

type app struct {
	ctx      *context.Context
	netstack *netstack.NetstackInterface
}

func (a *app) getFilterStatus() {
	enabled, err := a.netstack.GetFilterStatus()
	if err != nil {
		fmt.Println("GetFilterStatus error:", err)
		return
	}
	if enabled {
		fmt.Println("enabled")
	} else {
		fmt.Println("disabled")
	}
}

func (a *app) setFilterStatus(enableDisable string) {
	switch enableDisable {
	case "enable":
		a.netstack.SetFilterStatus(true)
	case "disable":
		a.netstack.SetFilterStatus(false)
	default:
		usage()
	}
}

func usage() {
	fmt.Printf("Usage: %s [enable|disable]\n", commandName)
	os.Exit(1)
}

func main() {
	a := &app{ctx: context.CreateFromStartupInfo()}
	req, pxy, err := netstack.NewNetstackInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	a.netstack = pxy
	defer a.netstack.Close()
	a.ctx.ConnectToEnvService(req)

	// TODO: more functions to support here.
	switch len(os.Args) {
	case 1:
		a.getFilterStatus()
	case 2:
		a.setFilterStatus(os.Args[1])
	default:
		usage()
	}
}
