// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"errors"
	"sync"
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
	processor func(*Package, *Package, Source, *PackageSet) error
	src       Source
	pkgs      *PackageSet
	// runGate guarantees that only one SourceMonitor is running a check at
	// any given time, which prevents updates from different sources from
	// stepping on each other
	runGate *sync.Mutex

	oneShot    chan struct{}
	muOneShots sync.Mutex
	oneShotQ   []*PackageSet
}

const oneShotLen = 100

var ErrCheckRejected = errors.New("pkg_src: Too many pending requests")

// NewSourceMonitr creates a new SourceMonitor object that manages periodic and
// one-off update requests for a Source. The 'proc' func will be called when a
// matching update is available from the source.
func NewSourceMonitor(s Source, pkgs *PackageSet, proc func(*Package, *Package, Source, *PackageSet) error,
	m *sync.Mutex) *SourceMonitor {
	return &SourceMonitor{src: s,
		pkgs:      pkgs,
		processor: proc,
		runGate:   m,
		// give the channel a one slot buffer so we can guarantee that
		// GetUpdates never blocks for sending on its channel
		oneShot:    make(chan struct{}, 1),
		muOneShots: sync.Mutex{},
		oneShotQ:   []*PackageSet{}}
}

// Run begins the SourceMonitor which will then check for a package at
// the time specified in UpdateRequest.UpdateInterval. This function blocks
// until after Stop() is called.
func (sm *SourceMonitor) Run() {
	sm.done = make(chan struct{})
	t := time.Now()
	sm.ticker = newTicker(sm.src.CheckInterval())
	sm.check(t, sm.pkgs)
	for {
		select {
		case t, _ := <-sm.ticker.C:
			sm.check(t, sm.pkgs)
		case _, ok := <-sm.oneShot:
			if ok {
				// TODO(jmatt) check that we can do a check
				// while respecting rate limits, otherwise
				// re-schedule the check.
				sm.doOneShots()
			}
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
	close(sm.oneShot)
	sm.oneShot = nil
}

// GetUpdates takes a PackageSet and does a one-time check for any available
// updates. The check happens as soon as possible, subject to any query rate
// limitations that might be imposed by the source. This function may return
// CheckRejectedErr if the SourceMonitor's queue of one-time check requests
// is too large.
func (sm *SourceMonitor) GetUpdates(pkgs *PackageSet) error {
	sm.muOneShots.Lock()
	defer sm.muOneShots.Unlock()
	qLen := len(sm.oneShotQ)
	if qLen >= oneShotLen {
		return ErrCheckRejected
	}
	sm.oneShotQ = append(sm.oneShotQ, pkgs)

	// if we went from 0->1 one shot requests, ping the channel
	if qLen == 0 {
		sm.oneShot <- struct{}{}
	}
	return nil
}

// Stop stops the SourceMonitor.
func (sm *SourceMonitor) Stop() {
	sm.ticker.Stop()
	sm.done <- struct{}{}
}

// doOneShots clears the one shot queue safely and then checks the source
// for those packages
func (sm *SourceMonitor) doOneShots() {
	// copy the queue contents and empty it
	sm.muOneShots.Lock()
	c := make([]*PackageSet, len(sm.oneShotQ))
	copy(c, sm.oneShotQ)
	sm.oneShotQ = []*PackageSet{}
	sm.muOneShots.Unlock()

	m := NewPackageSet()

	// collapse package sets
	for i, _ := range c {
		pkgs := c[i].Packages()
		for _, p := range pkgs {
			m.Add(p)
		}
	}

	sm.check(time.Now(), m)
}

func (sm *SourceMonitor) check(t time.Time, ps *PackageSet) {
	if sm.LatestCheck.Sub(t) >= 0 {
		return
	}

	sm.runGate.Lock()
	defer sm.runGate.Unlock()

	// track the number of workers updating specific packages
	var workers sync.WaitGroup
	pkgs := ps.Packages()
	updates, _ := sm.src.AvailableUpdates(pkgs)

	workers.Add(len(pkgs))
	for _, pkg := range pkgs {
		update, ok := updates[*pkg]
		if !ok {
			workers.Done()
			continue
		}

		go func(orig *Package, upd *Package) {
			if err := sm.processor(upd, orig, sm.src, sm.pkgs); err != nil {
				Log.Printf("error processing package %v\n", err)
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
