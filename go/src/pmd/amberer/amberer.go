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
	"syscall/zx"
)

type AmberClient interface {
	PackagesActivated(merkles []string)
	PackagesFailed(merkles []string, status zx.Status, blob_merkle string)
	GetBlob(merkle string)
}

type realAmberClient struct {
	mu    sync.RWMutex
	proxy *amber.ControlInterface
}

func (am *realAmberClient) PackagesActivated(merkles []string) {
	go func() {
		amber, err := am.get()
		if err != nil {
			log.Printf("pkgfs: amber.PackagesActivated(%v) failed: %s", merkles, err)
			return
		}

		err = amber.PackagesActivated(merkles)
		if am.checkErr(err) {
			log.Printf("pkgfs: amber.PackagesActivated(%v) failed: %s", merkles, err)
		}
	}()
}

func (am *realAmberClient) PackagesFailed(merkles []string, status zx.Status, blobMerkle string) {
	go func() {
		amber, err := am.get()
		if err != nil {
			log.Printf("pkgfs: amber.PackagesFailed(%v, %s) failed: %s", merkles, blobMerkle, err)
			return
		}

		err = amber.PackagesFailed(merkles, int32(status), blobMerkle)
		if am.checkErr(err) {
			log.Printf("pkgfs: amber.PackagesFailed(%v, %s) failed: %s", merkles, blobMerkle, err)
		}
	}()
}

func (am *realAmberClient) GetBlob(merkle string) {
	amber, err := am.get()
	if err != nil {
		log.Printf("pkgfs: amber.GetBlob(%q) failed: %s", merkle, err)
		return
	}

	err = amber.GetBlob(merkle)
	if am.checkErr(err) {
		log.Printf("pkgfs: amber.GetBlob(%q) failed: %s", merkle, err)
	}
}

var _ AmberClient = &realAmberClient{}

func NewAmberClient() AmberClient {
	return &realAmberClient{}
}

func (am *realAmberClient) get() (*amber.ControlInterface, error) {
	am.mu.RLock()
	var a = am.proxy
	am.mu.RUnlock()

	if a != nil {
		return a, nil
	}

	am.mu.Lock()
	defer am.mu.Unlock()

	req, pxy, err := amber.NewControlInterfaceRequest()
	if err != nil {
		return nil, err
	}
	context.CreateFromStartupInfo().ConnectToEnvService(req)
	am.proxy = pxy
	return pxy, nil
}

func (am *realAmberClient) checkErr(err error) bool {
	if err != nil {
		am.mu.Lock()
		if am.proxy != nil {
			am.proxy.Close()
		}
		am.proxy = nil
		am.mu.Unlock()
		return true
	}
	return false
}
