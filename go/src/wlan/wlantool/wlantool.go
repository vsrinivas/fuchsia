// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"
	"time"

	"app/context"
	"fidl/bindings"

	"garnet/public/lib/wlan/fidl/wlan_service"
)

const (
	cmdScan       = "scan"
	cmdConnect    = "connect"
	cmdDisconnect = "disconnect"
)

type ToolApp struct {
	ctx  *context.Context
	wlan *wlan_service.Wlan_Proxy
}

func (a *ToolApp) Scan(seconds uint8) {
	expiry := (time.Duration(seconds) + 5) * time.Second
	t := time.NewTimer(expiry)

	rxed := make(chan struct{})
	go func() {
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
					ap.Bssid, ap.LastRssi, prot, ap.Ssid)
			}
		}
		rxed <- struct{}{}
	}()

	select {
	case <-rxed:
		// Received scan results.
	case <-t.C:
		fmt.Printf("Scan timed out; aborting.\n")
	}

}

func (a *ToolApp) Connect(ssid string, passPhrase string, seconds uint8) {
	if len(ssid) > 32 {
		fmt.Println("ssid is too long")
		return
	}
	werr, err := a.wlan.Connect(wlan_service.ConnectConfig{ssid, passPhrase, seconds})
	if err != nil {
		fmt.Println("Error:", err)
	} else if werr.Code != wlan_service.ErrCode_Ok {
		fmt.Println("Error:", werr.Description)
	}
}

func (a *ToolApp) Disconnect() {
	werr, err := a.wlan.Disconnect()
	if err != nil {
		fmt.Println("Error:", err)
	} else if werr.Code != wlan_service.ErrCode_Ok {
		fmt.Println("Error:", werr.Description)
	}
}

var Usage = func() {
	fmt.Printf("Usage: %v %v [-t <timeout>]\n", os.Args[0], cmdScan)
	fmt.Printf("       %v %v [-p <passphrase>] [-t <timeout>] ssid\n", os.Args[0], cmdConnect)
	fmt.Printf("       %v %v\n", os.Args[0], cmdDisconnect)
}

func main() {
	scanFlagSet := flag.NewFlagSet("scan", flag.ExitOnError)
	scanTimeout := scanFlagSet.Int("t", 0, "scan timeout (1 - 255 seconds)")

	connectFlagSet := flag.NewFlagSet("connect", flag.ExitOnError)
	connectScanTimeout := connectFlagSet.Int("t", 0, "scan timeout (1 to 255 seconds)")
	connectPassPhrase := connectFlagSet.String("p", "", "pass-phrase (8 to 63 ASCII characters")

	a := &ToolApp{ctx: context.CreateFromStartupInfo()}
	r, p := a.wlan.NewRequest(bindings.GetAsyncWaiter())
	a.wlan = p
	defer a.wlan.Close()
	a.ctx.ConnectToEnvService(r)

	if len(os.Args) < 2 {
		Usage()
		return
	}

	cmd := os.Args[1]
	switch cmd {
	case cmdScan:
		scanFlagSet.Parse(os.Args[2:])
		if *scanTimeout != 0 && !IsValidTimeout(*scanTimeout) {
			scanFlagSet.PrintDefaults()
			return
		}
		if scanFlagSet.NArg() != 0 {
			Usage()
			return
		}
		a.Scan(uint8(*scanTimeout))
	case cmdConnect:
		connectFlagSet.Parse(os.Args[2:])
		if *connectScanTimeout != 0 && !IsValidTimeout(*connectScanTimeout) {
			connectFlagSet.PrintDefaults()
			return
		}
		if *connectPassPhrase != "" && !IsValidPSKPassPhrase(*connectPassPhrase) {
			connectFlagSet.PrintDefaults()
			return
		}
		if connectFlagSet.NArg() != 1 {
			Usage()
			return
		}
		ssid := connectFlagSet.Arg(0)
		a.Connect(ssid, *connectPassPhrase, uint8(*connectScanTimeout))
	case cmdDisconnect:
		disconnectFlagSet := flag.NewFlagSet("disconnect", flag.ExitOnError)
		disconnectFlagSet.Parse(os.Args[2:])
		if disconnectFlagSet.NArg() != 0 {
			Usage()
			return
		}
		a.Disconnect()
	default:
		Usage()
	}
}

func IsValidTimeout(timeout int) bool {
	if timeout < 1 || timeout > 255 {
		fmt.Println("Timeout must be 1 to 255 seconds")
		return false
	}
	return true
}

func IsValidPSKPassPhrase(passPhrase string) bool {
	// len(s) can be used because PSK always operates on ASCII characters.
	if len(passPhrase) < 8 || len(passPhrase) > 63 {
		fmt.Println("Pass phrase must be 8 to 63 characters")
		return false
	}
	for _, c := range passPhrase {
		if c < 32 || c > 126 {
			fmt.Println("Pass phrase must be ASCII characters")
			return false
		}
	}
	return true
}
