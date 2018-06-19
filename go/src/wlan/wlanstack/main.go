// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"app/context"
	"fidl/bindings"

	"fidl/fuchsia/wlan/stats"
	wlan_service "fidl/fuchsia/wlan/service"
	"netstack/watcher"
	"wlan/wlan"

	"fmt"
	"log"
	"sync"
	"syscall/zx"
)

const debug = false

type Wlanstack struct {
	cfg   *wlan.Config
	apCfg *wlan.APConfig

	mu     sync.Mutex
	client []*wlan.Client
}

func (ws *Wlanstack) Scan(sr wlan_service.ScanRequest) (res wlan_service.ScanResult, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.ScanResult{
			wlan_service.Error{
				wlan_service.ErrCodeNotFound,
				"No wlan interface found"},
			nil,
		}, nil
	}
	respC := make(chan *wlan.CommandResult, 1)
	cli.PostCommand(wlan.CmdScan, &sr, respC)

	resp := <-respC
	if resp.Err != nil {
		return wlan_service.ScanResult{
			*resp.Err,
			nil,
		}, nil
	}
	waps, ok := resp.Resp.([]wlan.AP)
	if !ok {
		return wlan_service.ScanResult{
			wlan_service.Error{
				wlan_service.ErrCodeInternal,
				"Internal error"},
			nil,
		}, nil
	}
	aps := []wlan_service.Ap{}
	for _, wap := range waps {
		ap := wlan.ConvertWapToAp(wap)
		aps = append(aps, ap)
	}
	return wlan_service.ScanResult{
		wlan_service.Error{wlan_service.ErrCodeOk, "OK"},
		&aps,
	}, nil
}

func (ws *Wlanstack) Connect(sc wlan_service.ConnectConfig) (wserr wlan_service.Error, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.Error{
			wlan_service.ErrCodeNotFound,
			"No wlan interface found"}, nil
	}
	cfg := wlan.NewConfig()
	cfg.SSID = sc.Ssid
	cfg.Password = sc.PassPhrase
	cfg.ScanInterval = int(sc.ScanInterval)
	cfg.BSSID = sc.Bssid
	respC := make(chan *wlan.CommandResult, 1)
	cli.PostCommand(wlan.CmdSetScanConfig, cfg, respC)

	resp := <-respC
	if resp.Err != nil {
		return *resp.Err, nil
	}
	return wlan_service.Error{wlan_service.ErrCodeOk, "OK"}, nil
}

func (ws *Wlanstack) Disconnect() (wserr wlan_service.Error, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.Error{wlan_service.ErrCodeNotFound, "No wlan interface found"}, nil
	}
	respC := make(chan *wlan.CommandResult, 1)
	var noArgs interface{}
	cli.PostCommand(wlan.CmdDisconnect, noArgs, respC)

	resp := <-respC
	if resp.Err != nil {
		return *resp.Err, nil
	}
	return wlan_service.Error{wlan_service.ErrCodeOk, "OK"}, nil
}

func (ws *Wlanstack) Status() (res wlan_service.WlanStatus, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.WlanStatus{
			wlan_service.Error{
				wlan_service.ErrCodeNotFound,
				"No wlan interface found"},
			wlan_service.StateUnknown,
			nil,
		}, nil
	}
	return cli.Status(), nil
}

func (ws *Wlanstack) StartBss(sc_cfg wlan_service.BssConfig) (wserr wlan_service.Error, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.Error{wlan_service.ErrCodeNotFound, "No wlan interface found"}, nil
	}

	cfg := wlan.NewAPConfig(sc_cfg.Ssid, sc_cfg.BeaconPeriod, sc_cfg.DtimPeriod, sc_cfg.Channel)
	respC := make(chan *wlan.CommandResult, 1)
	cli.PostCommand(wlan.CmdStartBSS, cfg, respC)

	resp := <-respC
	if resp.Err != nil {
		return *resp.Err, nil
	}
	return wlan_service.Error{wlan_service.ErrCodeOk, "OK"}, nil
}

func (ws *Wlanstack) StopBss() (wserr wlan_service.Error, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.Error{wlan_service.ErrCodeNotFound, "No wlan interface found"}, nil
	}

	respC := make(chan *wlan.CommandResult, 1)
	var noArgs interface{}
	cli.PostCommand(wlan.CmdStopBSS, noArgs, respC)

	resp := <-respC
	if resp.Err != nil {
		return *resp.Err, nil
	}
	return wlan_service.Error{wlan_service.ErrCodeOk, "OK"}, nil
}

func (ws *Wlanstack) Stats() (result wlan_service.WlanStats, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.WlanStats{
			wlan_service.Error{
				wlan_service.ErrCodeNotFound,
				"No wlan interface found"},
			stats.IfaceStats{},
		}, nil
	}

	respC := make(chan *wlan.CommandResult, 1)
	var noArgs interface{}
	cli.PostCommand(wlan.CmdStats, noArgs, respC)

	resp := <-respC
	if resp.Err != nil {
		return wlan_service.WlanStats{*resp.Err, stats.IfaceStats{}}, nil
	}

	return wlan_service.WlanStats{wlan_service.Error{wlan_service.ErrCodeOk, "OK"}, resp.Resp.(stats.IfaceStats)}, nil
}

func main() {
	log.SetFlags(0)
	log.SetPrefix("wlanstack: ")
	log.Print("started")

	ws := &Wlanstack{}

	service := &bindings.BindingSet{}
	ctx := context.CreateFromStartupInfo()
	ctx.OutgoingService.AddService(wlan_service.WlanName, func(c zx.Channel) error {
		_, err := service.Add(&wlan_service.WlanStub{Impl: ws}, c, nil)
		return err
	})
	// Create 2 goroutines to handle up to 2 blocking commands simultaneously
	go bindings.Serve()
	go bindings.Serve()
	ctx.Serve()

	ws.readConfigFile()
	ws.readAPConfigFile()

	const ethdir = "/dev/class/wlanif"
	wt, err := watcher.NewWatcher(ethdir)
	if err != nil {
		log.Fatalf("wlanif: %v", err)
	}
	log.Printf("watching for wlan devices")

	for name := range wt.C {
		path := ethdir + "/" + name
		if err := ws.addDevice(path); err != nil {
			log.Printf("failed to add wlan device %s: %v", path, err)
		}
	}
}

func (ws *Wlanstack) readConfigFile() {
	// TODO(tkilbourn): monitor this file for changes
	// TODO(tkilbourn): replace this with a FIDL interface
	const configFile = "/pkg/data/config.json"

	cfg, err := wlan.ReadConfigUser()
	if err != nil {
		if cfg, err = wlan.ReadConfigFromFile(configFile); err != nil {
			log.Printf("[W] could not open config (%v)", configFile)
			return
		}
	}
	if debug {
		log.Printf("cfg read: %+v, err: %+v", cfg, err)
	}
	ws.cfg = cfg
}

func (ws *Wlanstack) readAPConfigFile() {
	// TODO(hahnr): Replace once FIDL api is in place
	const apConfigFile = "/pkg/data/ap_config.json"

	cfg, err := wlan.ReadAPConfigFromFile(apConfigFile)
	if err != nil {
		log.Printf("[W] could not open config (%v)", apConfigFile)
		return
	}
	ws.apCfg = cfg
}

func (ws *Wlanstack) addDevice(path string) error {
	cli, err := wlan.NewClient(path, ws.cfg, ws.apCfg)
	if err != nil {
		return err
	}
	if cli == nil {
		return fmt.Errorf("wlan.NewClient returned nil!!!")
	}
	log.Printf("found wlan device %q", path)

	ws.mu.Lock()
	ws.client = append(ws.client, cli)
	ws.mu.Unlock()

	go cli.Run()
	return nil
}

func (ws *Wlanstack) getCurrentClient() *wlan.Client {
	// Use the last client.
	// TODO: Add a mechanism to choose a client
	ws.mu.Lock()
	defer ws.mu.Unlock()
	ncli := len(ws.client)
	if ncli > 0 {
		return ws.client[ncli-1]
	} else {
		return nil
	}
}
