// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ipcserver

import (
	"amber/daemon"
	"amber/pkg"
	"bytes"
	"os"
	"sync"
	"sync/atomic"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"
	"time"
)

const merkleA = "7c59df285871ae26a437fe09b169360d6dd35619f27f67ee00b363351d0807ff"
const merkleB = "dd4286f14dde873120982a173ff64008c0df18eb803382555fb3a2810936c0e1"
const unrequestedMerkle = "a443cb639dfc7c83bf745da7b81008df0ded08dd5f374a6198ba44a8cbbe586c"

var sampleGetResA = daemon.GetResult{
	Update: pkg.Package{Merkle: merkleA, Name: "testpkg", Version: "74"},
}
var sampleGetResB = daemon.GetResult{
	Update: pkg.Package{Merkle: merkleB, Name: "gkptset", Version: "90"},
}

type chanExpected struct {
	ipcChan *zx.Channel
	merkle  string
}

func TestCompleteUpdateRequest(t *testing.T) {
	writeReqCount := 0
	counterMu := &sync.Mutex{}
	a := make(chan string, 5)
	c := make(chan *completeUpdateRequest, 1)
	w := make(chan *startUpdateRequest, 1)

	writeFunc := func(r *daemon.GetResult, dst *os.File) (string, error) {
		counterMu.Lock()
		writeReqCount++
		counterMu.Unlock()
		go func() {
			a <- r.Update.Merkle
		}()
		return r.Update.Merkle, nil
	}

	m := NewActivationMonitor(c, w, a, dummyCreate, writeFunc)

	in, out, e := zx.NewChannel(0)
	if e != nil {
		t.Fatalf("Channel creation failed: %s", e)
	}
	req := completeUpdateRequest{
		pkgData:   &sampleGetResA,
		replyChan: &in,
	}

	go m.Do()

	c <- &req

	buf := make([]byte, 1024)
	sigs, err := zxwait.Wait(*out.Handle(), zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
		zx.Sys_deadline_after(zx.Duration((100 * time.Millisecond).Nanoseconds())))
	if err != nil {
		t.Fatalf("Failed reading response from channel %s", err)
	}

	if sigs&zx.SignalChannelReadable != zx.SignalChannelReadable {
		t.Fatalf("Channel was not readable, flags: %d", sigs)
	}

	sz, _, err := out.Read(buf, []zx.Handle{}, 0)
	if err != nil {
		t.Fatalf("Read from reply channel failed: %s", err)
	}

	if sz != uint32(len(merkleA)) || !bytes.Equal(buf[0:sz], []byte(merkleA)) {
		t.Fatalf("Wrong merkle root received, expected %q, got %q", merkleA, string(buf[0:sz]))
	}

	counterMu.Lock()
	if writeReqCount != 1 {
		t.Fatalf("Unexpected number of write requests, expected %d, found %d", 1, writeReqCount)
	}
	counterMu.Unlock()

	out.Close()
}

func TestNonWaitingRequest(t *testing.T) {
	writeReqCount := 0
	counterMu := &sync.Mutex{}
	a := make(chan string, 5)
	c := make(chan *completeUpdateRequest, 1)
	w := make(chan *startUpdateRequest, 1)
	writeFunc := func(r *daemon.GetResult, dst *os.File) (string, error) {
		counterMu.Lock()
		writeReqCount++
		counterMu.Unlock()
		go func() {
			a <- r.Update.Merkle
		}()
		return r.Update.Merkle, nil
	}
	m := NewActivationMonitor(c, w, a, dummyCreate, writeFunc)
	req := startUpdateRequest{
		pkgData: &sampleGetResA,
		wg:      &sync.WaitGroup{},
		err:     nil,
	}
	req.wg.Add(1)

	doneChan := make(chan struct{})
	go m.Do()
	go func() {
		select {
		case <-doneChan:
			return
		case <-time.After(100 * time.Millisecond):
			t.Fatalf("Test took too long!")
		}
	}()
	w <- &req
	req.wg.Wait()
	close(doneChan)
	if req.err != nil {
		t.Fatalf("Write request failed: %s", req.err)
	}

	counterMu.Lock()
	if writeReqCount != 1 {
		t.Fatalf("Unexpected number of write requests, expected %d, found %d", 1, writeReqCount)
	}
	counterMu.Unlock()
}

func TestOtherPackageActivated(t *testing.T) {
	a := make(chan string, 5)
	c := make(chan *completeUpdateRequest, 1)
	w := make(chan *startUpdateRequest, 1)
	writeFunc := func(r *daemon.GetResult, dst *os.File) (string, error) {
		go func() {
			a <- unrequestedMerkle
		}()
		return r.Update.Merkle, nil
	}
	m := NewActivationMonitor(c, w, a, dummyCreate, writeFunc)

	in, out, e := zx.NewChannel(0)
	if e != nil {
		t.Fatalf("Channel creation failed: %s", e)
	}
	req := completeUpdateRequest{
		pkgData:   &sampleGetResA,
		replyChan: &in,
	}

	go m.Do()

	c <- &req

	buf := make([]byte, 1024)
	sigs, err := zxwait.Wait(*out.Handle(), zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
		zx.Sys_deadline_after(zx.Duration((100 * time.Millisecond).Nanoseconds())))
	timedOut := true
	if err != nil && err.(zx.Error).Status != zx.ErrTimedOut {
		t.Errorf("Failed reading response from channel %s", err)
		timedOut = false
	}

	if sigs&zx.SignalChannelReadable == zx.SignalChannelReadable {
		t.Errorf("Channel was readable, but should not have been, flags: %d", sigs)
		sz, _, err := out.Read(buf, []zx.Handle{}, 0)
		if err != nil {
			t.Errorf("Read from reply channel failed: %s", err)
		}

		t.Fatalf("Value read from channel %q", string(buf[0:sz]))
	}

	if sigs&zx.SignalChannelPeerClosed == zx.SignalChannelPeerClosed {
		t.Errorf("Channel was closed, but expected to be open")
	}

	if !timedOut {
		t.Fail()
	}

	out.Close()
	in.Close()
}

func TestHighRequestVolume(t *testing.T) {
	counterMu := &sync.Mutex{}
	writeReqCount := 0
	a := make(chan string, 5)
	c := make(chan *completeUpdateRequest)
	w := make(chan *startUpdateRequest, 1)
	writeFunc := func(r *daemon.GetResult, dst *os.File) (string, error) {
		counterMu.Lock()
		writeReqCount++
		counterMu.Unlock()
		return r.Update.Merkle, nil
	}

	// mock a create function which returns os.ErrExist when a fetch for the
	// package is in progress
	muActiveFiles := &sync.Mutex{}
	activeFiles := make(map[string]struct{})
	createCount := int32(0)
	createFunc := func(r *daemon.GetResult) (*os.File, error) {
		return mockCreate(r, &createCount, muActiveFiles, activeFiles)
	}

	m := NewActivationMonitor(c, w, a, createFunc, writeFunc)

	reqCount := 1000
	reqs, outChans := makeCompletionReqs(reqCount, &sampleGetResA, t)

	go m.Do()

	wg := &sync.WaitGroup{}
	ranges := []int{0, 200, 400, 600, 800}
	wg.Add(len(ranges))
	for _, s := range ranges {
		go func(start int) {
			for i, r := range reqs[start : start+200] {
				c <- r

				// produce availability events at somewhat irregular intervals and
				// also remove these from the list of active files
				if start+i == start+start/10 {
					// remove this update from the active files set
					muActiveFiles.Lock()
					delete(activeFiles, reqs[0].pkgData.Update.Merkle)
					muActiveFiles.Unlock()
					a <- reqs[0].pkgData.Update.Merkle
				}
			}
			wg.Done()
		}(s)
	}

	go func() {
		// do a final availability event once all completion requests have been submitted
		wg.Wait()
		a <- reqs[0].pkgData.Update.Merkle
	}()

	deadline := zx.Sys_deadline_after(zx.Duration(5 * time.Second.Nanoseconds()))
	readChannelsAndVerify(deadline, outChans, t)

	counterMu.Lock()
	if writeReqCount > len(ranges)+1 {
		t.Fatalf("Unexpected number of write requests, expected no more than %d, found %d",
			len(ranges)+1, writeReqCount)
	}
	if createCount != int32(reqCount) {
		t.Fatalf("Create count %d does not match expected fo %d", createCount, reqCount)
	}
	counterMu.Unlock()
}

func TestMultipleTargets(t *testing.T) {
	counterMu := &sync.Mutex{}
	reqCounts := make(map[string]int)
	a := make(chan string, 5)
	c := make(chan *completeUpdateRequest)
	w := make(chan *startUpdateRequest, 1)
	writeFunc := func(r *daemon.GetResult, dst *os.File) (string, error) {
		counterMu.Lock()
		reqCounts[r.Update.Merkle] = reqCounts[r.Update.Merkle] + 1
		counterMu.Unlock()
		return r.Update.Merkle, nil
	}

	// mock a create function which returns os.ErrExist when a fetch for the
	// package is in progress
	muActiveFiles := &sync.Mutex{}
	activeFiles := make(map[string]struct{})
	createCount := int32(0)
	createFunc := func(r *daemon.GetResult) (*os.File, error) {
		return mockCreate(r, &createCount, muActiveFiles, activeFiles)
	}

	m := NewActivationMonitor(c, w, a, createFunc, writeFunc)

	reqCount := 1000
	reqsA, outChans := makeCompletionReqs(reqCount, &sampleGetResA, t)
	reqsB, outChans2 := makeCompletionReqs(reqCount, &sampleGetResB, t)
	outChans = append(outChans, outChans2...)

	go m.Do()

	wgA := &sync.WaitGroup{}
	wgB := &sync.WaitGroup{}
	ranges := []int{0, 200, 400, 600, 800}
	wgA.Add(len(ranges))
	wgB.Add(len(ranges))
	for _, s := range ranges {
		go func(start int) {
			for i, r := range reqsA[start : start+200] {
				c <- r

				// produce availability events at somewhat irregular intervals
				if start+i == start+start/10 {
					// remove this update from the active files set
					muActiveFiles.Lock()
					delete(activeFiles, reqsA[0].pkgData.Update.Merkle)
					muActiveFiles.Unlock()
					a <- reqsA[0].pkgData.Update.Merkle
				}
			}
			wgA.Done()
		}(s)

		go func(start int) {
			for i, r := range reqsB[start : start+200] {
				c <- r

				// produce availability events at somewhat irregular intervals
				if start+i == start+start/10 {
					// remove this update from the active files set
					muActiveFiles.Lock()
					delete(activeFiles, reqsB[0].pkgData.Update.Merkle)
					muActiveFiles.Unlock()
					a <- reqsB[0].pkgData.Update.Merkle
				}
			}
			wgB.Done()
		}(s)
	}

	go func() {
		// do a final availability event once all completion requests have been submitted
		wgA.Wait()
		a <- reqsA[0].pkgData.Update.Merkle
	}()
	go func() {
		// do a final availability event once all completion requests have been submitted
		wgB.Wait()
		a <- reqsB[0].pkgData.Update.Merkle
	}()

	deadline := zx.Sys_deadline_after(5 * zx.Duration(time.Second.Nanoseconds()))
	readChannelsAndVerify(deadline, outChans, t)
	counterMu.Lock()
	for _, count := range reqCounts {
		if count > len(ranges)+1 {
			t.Fatalf("Unexpected number of write requests, expected no more than %d, found %d",
				len(ranges)+1, count)
		}
	}
	counterMu.Unlock()
}

func TestUnactivatedTarget(t *testing.T) {
	counterMu := &sync.Mutex{}
	writeReqCount := 0
	a := make(chan string, 5)
	c := make(chan *completeUpdateRequest)
	w := make(chan *startUpdateRequest, 1)
	writeFunc := func(r *daemon.GetResult, dst *os.File) (string, error) {
		counterMu.Lock()
		writeReqCount++
		counterMu.Unlock()
		return r.Update.Merkle, nil
	}

	// mock a create function which returns os.ErrExist when a fetch for the
	// package is in progress
	muActiveFiles := &sync.Mutex{}
	activeFiles := make(map[string]struct{})
	createCount := int32(0)
	createFunc := func(r *daemon.GetResult) (*os.File, error) {
		return mockCreate(r, &createCount, muActiveFiles, activeFiles)
	}

	m := NewActivationMonitor(c, w, a, createFunc, writeFunc)

	reqCount := 100
	reqsPos, outChansPos := makeCompletionReqs(reqCount, &sampleGetResA, t)
	_, outChansNeg := makeCompletionReqs(1, &sampleGetResA, t)
	outChanNeg := outChansNeg[0]

	go m.Do()

	wg := &sync.WaitGroup{}
	ranges := []int{0, 20, 40, 60, 80}
	wg.Add(len(ranges))
	for _, s := range ranges {
		go func(start int) {
			for i, r := range reqsPos[start : start+20] {
				c <- r

				// produce availability events at somewhat irregular intervals
				if start+i == start+start/10 {
					// remove this update from the active files set
					muActiveFiles.Lock()
					delete(activeFiles, reqsPos[0].pkgData.Update.Merkle)
					muActiveFiles.Unlock()
					a <- reqsPos[0].pkgData.Update.Merkle
				}
			}
			wg.Done()
		}(s)
	}

	go func() {
		// do a final availability event once all completion requests have been submitted
		wg.Wait()
		a <- reqsPos[0].pkgData.Update.Merkle
	}()

	deadline := zx.Sys_deadline_after(zx.Duration(time.Second.Nanoseconds()))
	readChannelsAndVerify(deadline, outChansPos, t)

	counterMu.Lock()
	if writeReqCount > (len(ranges) + 1) {
		t.Fatalf("Unexpected number of write requests, expected no more than %d, found %d",
			len(ranges)+1, writeReqCount)
	}
	counterMu.Unlock()

	deadline = zx.Sys_deadline_after(zx.Duration((time.Millisecond * 100).Nanoseconds()))
	sigs, err := zxwait.Wait(*outChanNeg.ipcChan.Handle(),
		zx.SignalChannelPeerClosed|zx.SignalChannelReadable, deadline)
	if err == nil || err != nil && err.(zx.Error).Status != zx.ErrTimedOut {
		t.Fatal("Request should have timed out, but did not")
	}

	if sigs&zx.SignalChannelReadable == zx.SignalChannelReadable {
		t.Fatalf("Channel was readable, but should not have been")
	}
	outChanNeg.ipcChan.Close()
}

func readChannelsAndVerify(deadline zx.Time, chEx []*chanExpected, t *testing.T) {
	buf := make([]byte, 1024)
	for _, c := range chEx {
		ch := c.ipcChan
		sigs, err := zxwait.Wait(*ch.Handle(), zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
			deadline)
		if err != nil {
			t.Fatalf("Failed reading response from channel %s", err)
		}

		if sigs&zx.SignalChannelReadable != zx.SignalChannelReadable {
			t.Fatalf("Channel was not readable, flags: %d", sigs)
		}

		sz, _, err := ch.Read(buf, []zx.Handle{}, 0)
		if err != nil {
			t.Fatalf("Read from reply channel failed: %s", err)
		}

		if sz != uint32(len(c.merkle)) || !bytes.Equal(buf[0:sz], []byte(c.merkle)) {
			t.Fatalf("Wrong merkle root received, expected %q, got %q", c.merkle, string(buf[0:sz]))
		}
		ch.Close()
	}
}

func makeCompletionReqs(count int, getRes *daemon.GetResult, t *testing.T) (
	[]*completeUpdateRequest, []*chanExpected) {
	reqs := make([]*completeUpdateRequest, 0, count)
	chans := make([]*chanExpected, 0, count)
	for i := 0; i < count; i++ {
		in, out, e := zx.NewChannel(0)
		if e != nil {
			t.Fatalf("Could not create channel.")
		}
		req := &completeUpdateRequest{
			pkgData:   getRes,
			replyChan: &in,
		}
		reqs = append(reqs, req)
		chans = append(chans, &chanExpected{ipcChan: &out, merkle: getRes.Update.Merkle})
	}
	return reqs, chans
}

func mockCreate(r *daemon.GetResult, counter *int32, mu *sync.Mutex, activeFiles map[string]struct{}) (*os.File, error) {
	atomic.AddInt32(counter, 1)
	mu.Lock()
	defer mu.Unlock()
	if _, ok := activeFiles[r.Update.Merkle]; ok {
		return nil, os.ErrExist
	} else {
		activeFiles[r.Update.Merkle] = struct{}{}
		return nil, nil
	}
}

func dummyCreate(r *daemon.GetResult) (*os.File, error) {
	return nil, nil
}
