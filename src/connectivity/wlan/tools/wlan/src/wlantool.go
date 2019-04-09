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

	"fidl/fuchsia/wlan/common"

	wlan_service "fidl/fuchsia/wlan/service"
)

const (
	cmdScan       = "scan"
	cmdConnect    = "connect"
	cmdDisconnect = "disconnect"
	cmdStatus     = "status"
	cmdStartBSS   = "start-bss"
	cmdStopBSS    = "stop-bss"
	cmdStats      = "stats"
)

type ToolApp struct {
	ctx  *context.Context
	wlan *wlan_service.WlanInterface
}

// LINT.IfChange
func CbwToStr(cbw common.Cbw) string {
	switch cbw {
	case common.CbwCbw20:
		return " "
	case common.CbwCbw40:
		return "+"
	case common.CbwCbw40Below:
		return "-"
	case common.CbwCbw80:
		return "V"
	case common.CbwCbw160:
		return "W"
	case common.CbwCbw80P80:
		return "P"
	default:
		return "(unknown CBW)"
	}
}

func ChanToStr(ch common.WlanChan) string {
	return fmt.Sprintf("%3d%s", ch.Primary, CbwToStr(ch.Cbw))
}

// LINT.ThenChange(//src/connectivity/wlan/lib/common/cpp/channel.cpp)

func (a *ToolApp) Scan(seconds uint8) wlan_service.ErrCode {
	expiry := 25 * time.Second
	if seconds > 0 {
		expiry = time.Duration(seconds) * time.Second
	}

	t := time.NewTimer(expiry)

	rxed := make(chan struct{})
	go func() {
		res, err := a.wlan.Scan(wlan_service.ScanRequest{Timeout: seconds})
		if err != nil {
			fmt.Println("Error:", err)
		} else if res.Error.Code != wlan_service.ErrCodeOk {
			fmt.Println("Error:", res.Error.Description)
		} else {
			for _, ap := range *res.Aps {
				prot := " "
				if ap.IsSecure {
					prot = "*"
				}
				compatStr := "[NoSupport]"
				if ap.IsCompatible {
					compatStr = ""
				}
				fmt.Printf("%12s %x (RSSI: %4d) Chan %s %v %q\n",
					compatStr, ap.Bssid, ap.RssiDbm, ChanToStr(ap.Chan), prot, ap.Ssid)
			}
		}
		rxed <- struct{}{}
	}()

	select {
	case <-rxed:
		// Received scan results.
	case <-t.C:
		fmt.Printf("Scan timed out; aborting.\n")
		return wlan_service.ErrCodeInternal
	}
	return wlan_service.ErrCodeOk
}

func (a *ToolApp) Connect(ssid string, bssid string, passPhrase string,
	seconds uint8) wlan_service.ErrCode {
	if len(ssid) > 32 {
		fmt.Println("ssid is too long")
		return wlan_service.ErrCodeInvalidArgs
	}
	werr, err := a.wlan.Connect(wlan_service.ConnectConfig{
		Ssid:         ssid,
		PassPhrase:   passPhrase,
		ScanInterval: seconds,
		Bssid:        bssid,
	})
	if err != nil {
		fmt.Println("Error:", err)
		return wlan_service.ErrCodeInternal
	} else if werr.Code != wlan_service.ErrCodeOk {
		fmt.Println("Error:", werr.Description)
		return werr.Code
	}
	return wlan_service.ErrCodeOk
}

func (a *ToolApp) Disconnect() wlan_service.ErrCode {
	werr, err := a.wlan.Disconnect()
	if err != nil {
		fmt.Println("Error:", err)
		return wlan_service.ErrCodeInternal
	} else if werr.Code != wlan_service.ErrCodeOk {
		fmt.Println("Error:", werr.Description)
		return werr.Code
	}
	return wlan_service.ErrCodeOk
}

func (a *ToolApp) StartBSS(ssid string, beaconPeriod int32, dtimPeriod int32,
	channel uint8) wlan_service.ErrCode {
	if len(ssid) > 32 {
		fmt.Println("ssid is too long")
		return wlan_service.ErrCodeInvalidArgs
	}
	if len(ssid) == 0 {
		fmt.Println("ssid is too short")
		return wlan_service.ErrCodeInvalidArgs
	}
	werr, err := a.wlan.StartBss(wlan_service.BssConfig{
		Ssid:         ssid,
		BeaconPeriod: beaconPeriod,
		DtimPeriod:   dtimPeriod,
		Channel:      channel,
	})
	if err != nil {
		fmt.Println("Error:", err)
		return wlan_service.ErrCodeInternal
	} else if werr.Code != wlan_service.ErrCodeOk {
		fmt.Println("Error:", werr.Description)
		return werr.Code
	}
	return wlan_service.ErrCodeOk
}

func (a *ToolApp) StopBSS() wlan_service.ErrCode {
	werr, err := a.wlan.StopBss()
	if err != nil {
		fmt.Println("Error:", err)
		return wlan_service.ErrCodeInternal
	} else if werr.Code != wlan_service.ErrCodeOk {
		fmt.Println("Error:", werr.Description)
		return werr.Code
	}
	return wlan_service.ErrCodeOk
}

func (a *ToolApp) Status() wlan_service.ErrCode {
	res, err := a.wlan.Status()
	if err != nil {
		fmt.Println("Error:", err)
		return wlan_service.ErrCodeInternal
	} else if res.Error.Code != wlan_service.ErrCodeOk {
		fmt.Println("Error:", res.Error.Description)
		return res.Error.Code
	} else {
		state := "unknown"
		switch res.State {
		case wlan_service.StateBss:
			state = "starting-bss"
		case wlan_service.StateQuerying:
			state = "querying"
		case wlan_service.StateScanning:
			state = "scanning"
		case wlan_service.StateJoining:
			state = "joining"
		case wlan_service.StateAuthenticating:
			state = "authenticating"
		case wlan_service.StateAssociating:
			state = "associating"
		case wlan_service.StateAssociated:
			state = "associated"
		default:
			state = "unknown"
		}
		fmt.Printf("Status: %v\n", state)

		if res.CurrentAp != nil {
			ap := res.CurrentAp
			prot := " "
			if ap.IsSecure {
				prot = "*"
			}
			compatStr := "[NoSupport]"
			if ap.IsCompatible {
				compatStr = ""
			}

			fmt.Printf("%12s %x (RSSI: %d) Chan %s %v %q\n",
				compatStr, ap.Bssid, ap.RssiDbm, ChanToStr(ap.Chan), prot, ap.Ssid)
		}
	}
	return wlan_service.ErrCodeOk
}

func (a *ToolApp) ShowStats() wlan_service.ErrCode {
	result, err := a.wlan.Stats()
	if err != nil {
		fmt.Printf("Cannot get stats. Error: %+v\n", err)
		return wlan_service.ErrCodeInternal
	}
	stats := result.Stats
	fmt.Printf("Dispatcher stats:\n%+v\n", stats.DispatcherStats)
	if stats.MlmeStats != nil {
		fmt.Printf("\nMLME stats:\n%+v\n", stats.MlmeStats)
	}
	return wlan_service.ErrCodeOk
}

var Usage = func() {
	fmt.Printf("Usage: %v %v [-t <timeout>]\n", os.Args[0], cmdScan)
	fmt.Printf("       %v %v [-p <passphrase>] [-t <timeout>] [-b <bssid>] ssid\n", os.Args[0], cmdConnect)
	fmt.Printf("       %v %v\n", os.Args[0], cmdDisconnect)
	fmt.Printf("       %v %v\n", os.Args[0], cmdStatus)
	fmt.Printf("       %v %v [-b <beacon period>] [-d <DTIM period>] [-c channel] ssid\n", os.Args[0], cmdStartBSS)
	fmt.Printf("       %v %v\n", os.Args[0], cmdStopBSS)
	fmt.Printf("       %v %v\n", os.Args[0], cmdStats)
}

func mainFunc() wlan_service.ErrCode {
	scanFlagSet := flag.NewFlagSet(cmdScan, flag.ExitOnError)
	scanTimeout := scanFlagSet.Int("t", 0, "scan timeout (1 - 255 seconds)")

	connectFlagSet := flag.NewFlagSet(cmdConnect, flag.ExitOnError)
	connectScanTimeout := connectFlagSet.Int("t", 0, "scan timeout (1 to 255 seconds)")
	connectPassPhrase := connectFlagSet.String("p", "", "pass-phrase (8 to 63 ASCII characters")
	connectBSSID := connectFlagSet.String("b", "", "BSSID")

	a := &ToolApp{ctx: context.CreateFromStartupInfo()}
	req, pxy, err := wlan_service.NewWlanInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	a.wlan = pxy
	defer a.wlan.Close()
	a.ctx.ConnectToEnvService(req)

	if len(os.Args) < 2 {
		Usage()
		return wlan_service.ErrCodeInvalidArgs
	}

	cmd := os.Args[1]
	switch cmd {
	case cmdScan:
		scanFlagSet.Parse(os.Args[2:])
		if *scanTimeout != 0 && !IsValidTimeout(*scanTimeout) {
			scanFlagSet.PrintDefaults()
			return wlan_service.ErrCodeInvalidArgs
		}
		if scanFlagSet.NArg() != 0 {
			Usage()
			return wlan_service.ErrCodeInvalidArgs
		}
		return a.Scan(uint8(*scanTimeout))
	case cmdConnect:
		connectFlagSet.Parse(os.Args[2:])
		if *connectScanTimeout != 0 && !IsValidTimeout(*connectScanTimeout) {
			connectFlagSet.PrintDefaults()
			return wlan_service.ErrCodeInvalidArgs
		}
		if *connectPassPhrase != "" && !IsValidPSKPassPhrase(*connectPassPhrase) {
			connectFlagSet.PrintDefaults()
			return wlan_service.ErrCodeInvalidArgs
		}
		if *connectBSSID != "" && !IsValidBSSID(*connectBSSID) {
			connectFlagSet.PrintDefaults()
			return wlan_service.ErrCodeInvalidArgs
		}
		if connectFlagSet.NArg() != 1 {
			Usage()
			return wlan_service.ErrCodeInvalidArgs
		}
		ssid := connectFlagSet.Arg(0)
		return a.Connect(ssid, *connectBSSID, *connectPassPhrase, uint8(*connectScanTimeout))
	case cmdDisconnect:
		disconnectFlagSet := flag.NewFlagSet(cmdDisconnect, flag.ExitOnError)
		disconnectFlagSet.Parse(os.Args[2:])
		if disconnectFlagSet.NArg() != 0 {
			Usage()
			return wlan_service.ErrCodeInvalidArgs
		}
		return a.Disconnect()
	case cmdStatus:
		statusFlagSet := flag.NewFlagSet(cmdStatus, flag.ExitOnError)
		statusFlagSet.Parse(os.Args[2:])
		if statusFlagSet.NArg() != 0 {
			Usage()
			return wlan_service.ErrCodeInvalidArgs
		}
		return a.Status()
	case cmdStartBSS:
		startBSSFlagSet := flag.NewFlagSet(cmdStartBSS, flag.ExitOnError)
		startBSSBeaconPeriod := startBSSFlagSet.Int("b", 100, "Beacon period")
		startBSSDTIMPeriod := startBSSFlagSet.Int("d", 1, "DTIM period")
		startBSSChannel := startBSSFlagSet.Int("c", 48, "Channel")
		startBSSFlagSet.Parse(os.Args[2:])
		if startBSSFlagSet.NArg() != 1 {
			Usage()
			return wlan_service.ErrCodeInvalidArgs
		}
		ssid := startBSSFlagSet.Arg(0)
		return a.StartBSS(ssid, int32(*startBSSBeaconPeriod), int32(*startBSSDTIMPeriod), uint8(*startBSSChannel))
	case cmdStopBSS:
		stopBSSFlagSet := flag.NewFlagSet(cmdStartBSS, flag.ExitOnError)
		stopBSSFlagSet.Parse(os.Args[2:])
		if stopBSSFlagSet.NArg() != 0 {
			Usage()
			return wlan_service.ErrCodeInvalidArgs
		}
		return a.StopBSS()
	case cmdStats:
		statusFlagSet := flag.NewFlagSet(cmdStatus, flag.ExitOnError)
		statusFlagSet.Parse(os.Args[2:])
		if statusFlagSet.NArg() != 0 {
			Usage()
			return wlan_service.ErrCodeInvalidArgs
		}
		return a.ShowStats()
	default:
		Usage()
	}
	return wlan_service.ErrCodeOk
}

func main() {
	os.Exit(int(mainFunc()))
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

func IsValidBSSID(bssid string) bool {
	if len(bssid) != 17 {
		fmt.Println("BSSID must be 17 characters")
		return false
	}
	for i, c := range bssid {
		if (c < 'a' || c > 'f') && (c < 'A' || c > 'F') {
			if c < '0' || c > '9' {
				if c != ':' {
					fmt.Println("BSSID must be hexadecimal")
					return false
				} else if i%3 != 2 {
					fmt.Println("BSSID must be of the form xx:xx:xx:xx:xx:xx")
					return false
				}
			}
		}
	}
	return true
}
