// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"apps/netstack/watcher"
	"apps/wlan/wlan"

	"log"
)

var wlanConfig = wlan.NewConfig()

func main() {
	log.SetFlags(0)
	log.SetPrefix("wlanstack: ")
	log.Print("started")

	// TODO(tkilbourn): monitor this file for changes
	// TODO(tkilbourn): replace this with a FIDL interface
	const configFile = "/system/data/wlanstack/config.json"
	const overrideFile = "/system/data/wlanstack/override.json"
	var err error
	if wlanConfig, err = wlan.ReadConfigFromFile(overrideFile); err != nil {
		if wlanConfig, err = wlan.ReadConfigFromFile(configFile); err != nil {
			log.Printf("[W] could not open config (%v)", configFile)
		}
	}

	const ethdir = "/dev/class/ethernet"
	w, err := watcher.NewWatcher(ethdir)
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}
	log.Printf("watching for wlan devices")

	for name := range w.C {
		path := ethdir + "/" + name
		if err := tryAddEth(path); err != nil {
			log.Printf("failed to add wlan device %s: %v", path, err)
		}
	}
}

func tryAddEth(path string) error {
	log.Printf("trying ethernet device %q", path)

	w, err := wlan.NewClient(path, wlanConfig)
	if err != nil {
		return err
	}
	if w != nil {
		log.Printf("found wlan device %q", path)
		go w.Run()
	}

	return nil
}
