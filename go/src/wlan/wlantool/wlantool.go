// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"os"
	"strconv"

	"app/context"
	"fidl/bindings"

	"garnet/public/lib/wlan/fidl/wlan_service"
)

const (
	cmdScan    = "scan"
	cmdConnect = "connect"
)

type ToolApp struct {
	ctx  *context.Context
	wlan *wlan_service.Wlan_Proxy
}

func (a *ToolApp) Scan(seconds uint8) {
	res, err := a.wlan.Scan(wlan_service.ScanRequest{seconds})
	if err != nil {
		fmt.Println("Error:", err)
	} else if res.Error.Code != wlan_service.ErrCode_Ok {
		fmt.Println("Error:", res.Error.Description)
	} else {
		for _, ap := range *res.Aps {
			prot := " "
			if ap.IsSecure {
				prot = "*"
			}
			fmt.Printf("%x (RSSI: %d) %v %q\n",
				ap.Bssid, int8(ap.LastRssi), prot, ap.Ssid)
		}
	}
}

func (a *ToolApp) Connect(ssid string, seconds uint8) {
	if len(ssid) > 32 {
		fmt.Println("ssid is too long")
		return
	}
	werr, err := a.wlan.Connect(wlan_service.ConnectConfig{ssid, seconds})
	if err != nil {
		fmt.Println("Error:", err)
	} else if werr.Code != wlan_service.ErrCode_Ok {
		fmt.Println("Error:", werr.Description)
	}
}

func usage(progname string) {
	fmt.Printf("usage: %v %v [<timeout>]\n", progname, cmdScan)
	fmt.Printf("       %v %v <ssid> [<scan interval>]\n", progname, cmdConnect)
}

func main() {
	a := &ToolApp{ctx: context.CreateFromStartupInfo()}
	r, p := a.wlan.NewRequest(bindings.GetAsyncWaiter())
	a.wlan = p
	a.ctx.ConnectToEnvService(r)

	if len(os.Args) < 2 {
		usage(os.Args[0])
	} else {
		switch os.Args[1] {
		case cmdScan:
			if len(os.Args) == 3 {
				i, err := strconv.ParseInt(os.Args[2], 10, 8)
				if err != nil {
					fmt.Println("Error:", err)
				} else {
					a.Scan(uint8(i))
				}
			} else {
				a.Scan(0)
			}
		case cmdConnect:
			if len(os.Args) == 4 {
				i, err := strconv.ParseInt(os.Args[3], 10, 8)
				if err != nil {
					fmt.Println("Error:", err)
					return
				} else {
					a.Connect(os.Args[2], uint8(i))
				}
			} else if len(os.Args) == 3 {
				a.Connect(os.Args[2], 0)
			} else {
				usage(os.Args[0])
			}
		default:
			usage(os.Args[0])
		}
	}

	a.wlan.Close()
}
