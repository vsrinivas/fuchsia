// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"os"

	"app/context"
	"fidl/bindings"

	"apps/wlan/services/wlan_service"
)

type ToolApp struct {
	ctx  *context.Context
	wlan *wlan_service.Wlan_Proxy
}

func (a *ToolApp) Start() {
	r, p := a.wlan.NewRequest(bindings.GetAsyncWaiter())
	a.wlan = p
	a.ctx.ConnectToEnvService(r)
}

func (a *ToolApp) Scan() {
	res, err := a.wlan.Scan()
	if err != nil {
		fmt.Println("Error:", err)
	} else if res.Error.Code != wlan_service.ErrCode_Ok {
		fmt.Println("Error:", res.Error.Description)
	} else {
		for _, ap := range *res.Aps {
			fmt.Printf("%x (RSSI: %d) %q\n",
				ap.Bssid, int8(ap.LastRssi), ap.Ssid)
		}
	}
	a.wlan.Close()
}

func usage() {
	fmt.Printf("usage: wlantool scan\n")
}

func main() {
	a := &ToolApp{ctx: context.CreateFromStartupInfo()}
	a.Start()
	if len(os.Args) < 2 {
		usage()
		return
	}
	switch os.Args[1] {
	case "scan":
		a.Scan()
	default:
		usage()
		return
	}
}
