// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Amberer is a fire-and-forget auto-reconnecting proxy to the Amber Control Interface
package amberer

import (
	"app/context"
	"fidl/fuchsia/amber"
	"log"
	"sync"
)

var (
	mu    sync.RWMutex
	proxy *amber.ControlInterface
)

func get() (*amber.ControlInterface, error) {
	mu.RLock()
	var a = proxy
	mu.RUnlock()

	if a != nil {
		return a, nil
	}

	mu.Lock()
	defer mu.Unlock()

	req, pxy, err := amber.NewControlInterfaceRequest()
	if err != nil {
		return nil, err
	}
	context.CreateFromStartupInfo().ConnectToEnvService(req)
	proxy = pxy
	return pxy, nil
}

func checkErr(err error) bool {
	if err != nil {
		mu.Lock()
		if proxy != nil {
			proxy.Close()
		}
		proxy = nil
		mu.Unlock()
		return true
	}
	return false
}

func GetBlob(merkle string) {
	amber, err := get()
	if err != nil {
		log.Printf("pkgfs: amber.GetBlob(%q) failed: %s", merkle, err)
		return
	}

	err = amber.GetBlob(merkle)
	if checkErr(err) {
		log.Printf("pkgfs: amber.GetBlob(%q) failed: %s", merkle, err)
	}
}

func PackagesActivated(merkles []string) {
	amber, err := get()
	if err != nil {
		log.Printf("pkgfs: amber.PackagesActivated(%v) failed: %s", merkles, err)
		return
	}

	err = amber.PackagesActivated(merkles)
	if checkErr(err) {
		log.Printf("pkgfs: amber.PackagesActivated(%v) failed: %s", merkles, err)
	}
}
