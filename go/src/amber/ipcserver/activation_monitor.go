// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ipcserver

import (
	"amber/daemon"
	"amber/lg"
	"os"
	"sync"
	"syscall/zx"
)

type ActivationMonitor struct {
	waitList     map[string][]*zx.Channel
	write        func(*daemon.GetResult) (string, error)
	CompleteReqs <-chan *completeUpdateRequest
	WriteReqs    <-chan *startUpdateRequest
	Acts         <-chan string
}

type startUpdateRequest struct {
	pkgData *daemon.GetResult
	wg      *sync.WaitGroup
	err     error
}

type completeUpdateRequest struct {
	pkgData   *daemon.GetResult
	replyChan *zx.Channel
}

func NewActivationMonitor(cr <-chan *completeUpdateRequest, wr <-chan *startUpdateRequest,
	activations <-chan string, writeFunc func(*daemon.GetResult) (string, error)) *ActivationMonitor {
	a := ActivationMonitor{
		CompleteReqs: cr,
		WriteReqs:    wr,
		Acts:         activations,
		write:        writeFunc,
	}
	a.waitList = make(map[string][]*zx.Channel)
	return &a
}

func (am *ActivationMonitor) Do() {
	for {
		select {
		case r, ok := <-am.CompleteReqs:
			if !ok {
				am.CompleteReqs = nil
				break
			}
			lg.Log.Printf("Blocking update request received for %q\n", r.pkgData.Update.Merkle)
			if _, ok := am.waitList[r.pkgData.Update.Merkle]; !ok {
				if _, err := am.write(r.pkgData); err != nil {
					if os.IsExist(err) {
						lg.Log.Println("Package meta far already present, assuming package is available")
						r.replyChan.Write([]byte(r.pkgData.Update.Merkle), []zx.Handle{}, 0)
					}
					r.replyChan.Close()
					break
				}
			}

			am.registerReq(r)
		case r, ok := <-am.WriteReqs:
			if !ok {
				am.WriteReqs = nil
				break
			}
			if _, err := am.write(r.pkgData); err != nil {
				if !os.IsExist(err) {
					r.err = err
				}
			} else {
				am.setPkgInProgress(r)
			}
			r.wg.Done()
		case pkg, ok := <-am.Acts:
			if !ok {
				am.Acts = nil
			}
			lg.Log.Printf("Getting availablility for %q", pkg)
			if l, ok := am.waitList[pkg]; ok {
				for _, wtrChan := range l {
					wtrChan.Write([]byte(pkg), []zx.Handle{}, 0)
					wtrChan.Close()
				}
				delete(am.waitList, pkg)
			}
		}

		if am.CompleteReqs == nil && am.WriteReqs == nil && am.Acts == nil {
			lg.Log.Println("All channels closed, exiting.")
			return
		}
	}
}

func (am *ActivationMonitor) setPkgInProgress(req *startUpdateRequest) {
	_, ok := am.waitList[req.pkgData.Update.Merkle]
	if !ok {
		am.waitList[req.pkgData.Update.Merkle] = []*zx.Channel{}
	}
}

func (am *ActivationMonitor) registerReq(req *completeUpdateRequest) {
	chans, ok := am.waitList[req.pkgData.Update.Merkle]
	if !ok {
		chans = []*zx.Channel{}
	}
	am.waitList[req.pkgData.Update.Merkle] = append(chans, req.replyChan)
}
