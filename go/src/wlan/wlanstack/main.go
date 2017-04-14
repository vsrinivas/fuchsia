// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"apps/netstack/watcher"
	"apps/wlan/wlan"

	"log"
)

func main() {
	log.SetFlags(0)
	log.SetPrefix("wlanstack: ")
	log.Print("started")

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

	w, err := wlan.NewClient(path)
	if err != nil {
		return err
	}
	if w != nil {
		log.Printf("found wlan device %q", path)
		go w.Scan()
	}

	return nil
}
