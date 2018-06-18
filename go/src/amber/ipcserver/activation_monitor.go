// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ipcserver

import (
	"amber/daemon"
	"amber/lg"
	"fmt"
	"os"
	"sync"
	"syscall/zx"
)

type ActivationMonitor struct {
	waitList     map[string][]*zx.Channel
	write        func(*daemon.GetResult, *os.File) (string, error)
	create       func(*daemon.GetResult) (*os.File, error)
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
	activations <-chan string, createFunc func(*daemon.GetResult) (*os.File, error),
	writeFunc func(*daemon.GetResult, *os.File) (string, error)) *ActivationMonitor {
	a := ActivationMonitor{
		CompleteReqs: cr,
		WriteReqs:    wr,
		Acts:         activations,
		create:       createFunc,
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
			if err := am.writeMetaFAR(r.pkgData, r.replyChan); err != nil {
				r.pkgData.Err = err
				// if there was an error writing the meta FAR, don't listen for an activation
				break
			}
			am.registerReq(r)
		case r, ok := <-am.WriteReqs:
			if !ok {
				am.WriteReqs = nil
				break
			}

			if err := am.writeMetaFAR(r.pkgData, nil); err != nil {
				r.pkgData.Err = err
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

// writeMetaFAR writes out the meta FAR. It returns an error if the caller
// should not expect the write operation to product a package activation
// notification at a later date. If a replyChan is supplied it also sends
// an error back through it and closes it as approriate.
func (am *ActivationMonitor) writeMetaFAR(pkgData *daemon.GetResult, replyChan *zx.Channel) error {
	file, err := am.create(pkgData)
	if err == nil {
		if _, err := am.write(pkgData, file); err != nil {
			msg := fmt.Sprintf("could not write package meta file: %s", err)
			if replyChan != nil {
				sendError(replyChan, msg)
			}
			return err
		}
	} else if !os.IsExist(err) {
		if replyChan != nil {
			sendError(replyChan, err.Error())
		}
		return err
	}
	return nil
}

// sendError sends an error back through the client handle. This method will
// return an error if it is unable to send an error throug the client handle.
func sendError(replyChan *zx.Channel, msg string) error {
	lg.Log.Println(msg)
	signalErr := replyChan.Handle().SignalPeer(0, zx.SignalUser0)
	if signalErr != nil {
		lg.Log.Printf("signal failed: %s", signalErr)
		replyChan.Close()
	} else {
		replyChan.Write([]byte(msg), []zx.Handle{}, 0)
	}
	return signalErr
}
