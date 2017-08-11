// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"time"
)

// SourceMonitor takes a Source and polls it for Package updates at the interval
// specified by the Source. To check a PackageSet just once, GetUpdates can be
// used.
type SourceMonitor struct {
	LatestCheck time.Time
	ticker      *time.Ticker
	done        chan struct{}
	// takes an update Package, an original Package and a Source to get the
	// update Package content from.
	processor func(*GetResult, *PackageSet) error
	d         *Daemon
	pkgs      *PackageSet
	i         time.Duration
}

// NewSourceMonitr creates a new SourceMonitor object that manages periodic and
// one-off update requests for a Source. The 'proc' func will be called when a
// matching update is available from the source.
func NewSourceMonitor(d *Daemon, pkgs *PackageSet, proc func(*GetResult, *PackageSet) error,
	interval time.Duration) *SourceMonitor {
	return &SourceMonitor{
		d:         d,
		pkgs:      pkgs,
		processor: proc,
		i:         interval,
	}
}

// Run begins the SourceMonitor which will then check for a package at
// the time specified in UpdateRequest.UpdateInterval. This function blocks
// until after Stop() is called.
func (sm *SourceMonitor) Run() {
	sm.done = make(chan struct{})
	t := time.Now()
	sm.ticker = newTicker(sm.i)
	sm.check(t, sm.pkgs)
	for {
		select {
		case t, _ := <-sm.ticker.C:
			sm.check(t, sm.pkgs)
		case _, ok := <-sm.done:
			if ok {
				sm.stop()
			}
			return
		}
	}
}

func (sm *SourceMonitor) stop() {
	sm.ticker.Stop()
	close(sm.done)
	sm.done = nil
}

// Stop stops the SourceMonitor.
func (sm *SourceMonitor) Stop() {
	sm.ticker.Stop()
	sm.done <- struct{}{}
}

func (sm *SourceMonitor) check(t time.Time, ps *PackageSet) {
	if sm.LatestCheck.Sub(t) >= 0 {
		return
	}

	updates := sm.d.GetUpdates(ps)
	for _, up := range updates {
		if err := sm.processor(up, ps); err != nil {
			Log.Printf("error processing package %v\n", err)
		}
	}

	// record when the check finishes. If it took longer than the update
	// interval we can use this to avoid doing a flurry of checks once
	// the check completes
	sm.LatestCheck = time.Now()
}
