// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"sync"
	"time"
)

// SourceMonitor takes a Source and polls it for Package updates at the interval
// specified by the Source
type SourceMonitor struct {
	LatestCheck time.Time
	ticker      *time.Ticker
	done        chan struct{}
	processor   func(*Package, Source) error
	src         Source
	pkgs        *PackageSet
	// runGate guarantees that only one SourceMonitor is running a check at
	// any given time, which prevents updates from different sources from
	// stepping on each other
	runGate *sync.Mutex
}

// Run begins the SourceMonitor which will then check for a package at
// the time specified in UpdateRequest.UpdateInterval. This function blocks
// until after Stop() is called.
func (sm *SourceMonitor) Run() {
	sm.done = make(chan struct{})
	t := time.Now()
	sm.ticker = newTicker(sm.src.CheckInterval())
	sm.check(t)

	for {
		select {
		case t, _ := <-sm.ticker.C:
			sm.check(t)
		case _, ok := <-sm.done:
			if ok {
				sm.ticker.Stop()
				close(sm.done)
				sm.done = nil
			}
			return
		}
	}
}

// Stop stops the SourceMonitor.
func (sm *SourceMonitor) Stop() {
	sm.ticker.Stop()
	sm.done <- struct{}{}
}

func (sm *SourceMonitor) check(t time.Time) {
	if sm.LatestCheck.Sub(t) >= 0 {
		return
	}

	sm.runGate.Lock()
	defer sm.runGate.Unlock()
	fmt.Printf(".")

	// track the number of workers updating specific packages
	var workers sync.WaitGroup
	pkgs := sm.pkgs.Packages()
	updates, _ := sm.src.AvailableUpdates(pkgs)

	for _, pkg := range pkgs {
		update, ok := updates[*pkg]
		if !ok {
			continue
		}

		workers.Add(1)
		go func(orig *Package, upd *Package) {
			e := sm.processor(upd, sm.src)
			if e != nil {
				fmt.Printf("error processing package %v\n", e)
			} else {
				sm.pkgs.Remove(orig)
				sm.pkgs.Add(upd)
			}
			workers.Done()
		}(pkg, &update)
	}

	workers.Wait()
	// record when the check finishes. If it took longer than the update
	// interval we can use this to avoid doing a flurry of checks once
	// the check completes
	sm.LatestCheck = time.Now()
}
