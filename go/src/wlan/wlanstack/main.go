// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"app/context"
	"fidl/bindings"

	"syscall/mx"
	"syscall/mx/mxerror"

	"apps/wlan/services/wlan_service"
	"apps/wlan/wlan"
	"netstack/watcher"

	"log"
	"sync"
)

type Wlanstack struct {
	cfg   *wlan.Config
	stubs []*bindings.Stub

	mu     sync.Mutex
	client []*wlan.Client
}

func (ws *Wlanstack) Scan() (res wlan_service.ScanResult, err error) {
	cli := ws.getCurrentClient()
	if cli == nil {
		return wlan_service.ScanResult{
			wlan_service.Error{
				wlan_service.ErrCode_NotFound,
				"No wlan interface found"},
			nil,
		}, nil
	}
	respC := make(chan *wlan.CommandResult, 1)
	cli.PostCommand(wlan.CmdScan, respC)

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
				wlan_service.ErrCode_Internal,
				"Internal error"},
			nil,
		}, nil
	}
	aps := []wlan_service.Ap{}
	for _, wap := range waps {
		bssid := make([]uint8, len(wap.BSSID))
		copy(bssid, wap.BSSID[:])
		ap := wlan_service.Ap{bssid, wap.SSID, wap.LastRSSI}
		aps = append(aps, ap)
	}
	return wlan_service.ScanResult{
		wlan_service.Error{wlan_service.ErrCode_Ok, "OK"},
		&aps,
	}, nil
}

func (ws *Wlanstack) Bind(r wlan_service.Wlan_Request) {
	s := r.NewStub(ws, bindings.GetAsyncWaiter())
	ws.stubs = append(ws.stubs, s)
	go func() {
		for {
			if err := s.ServeRequest(); err != nil {
				if mxerror.Status(err) != mx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
	}()
}

func main() {
	log.SetFlags(0)
	log.SetPrefix("wlanstack: ")
	log.Print("started")

	ws := &Wlanstack{}

	ctx := context.CreateFromStartupInfo()
	ctx.OutgoingService.AddService(&wlan_service.Wlan_ServiceBinder{ws})
	ctx.Serve()

	ws.readConfigFile()

	const ethdir = "/dev/class/ethernet"
	wt, err := watcher.NewWatcher(ethdir)
	if err != nil {
		log.Fatalf("ethernet: %v", err)
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
	const configFile = "/system/data/wlanstack/config.json"
	const overrideFile = "/system/data/wlanstack/override.json"

	cfg, err := wlan.ReadConfigFromFile(overrideFile)
	if err != nil {
		if cfg, err = wlan.ReadConfigFromFile(configFile); err != nil {
			log.Printf("[W] could not open config (%v)", configFile)
			return
		}
	}
	ws.cfg = cfg
}

func (ws *Wlanstack) addDevice(path string) error {
	log.Printf("trying ethernet device %q", path)

	cli, err := wlan.NewClient(path, ws.cfg)
	if err != nil {
		return err
	}
	if cli == nil {
		// the device is not wlan. skip
		return nil
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
