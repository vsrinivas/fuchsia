// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"time"
)

// RequestRunner services a UpdateRequest.
type RequestRunner struct {
	*UpdateRequest
	LatestCheck time.Time
	*time.Ticker
	*SourceSet
	done      chan struct{}
	Processor func(*Package, Source) error
}

// Start begins the RequestRunner which will then check for a package at
// the time specified in UpdateRequest.UpdateInterval. This function blocks
// until after Stop() is called.
func (pm *RequestRunner) Run() {
	pm.done = make(chan struct{})
	t := time.Now()
	pm.Ticker = time.NewTicker(pm.UpdateRequest.UpdateInterval)
	pm.check(t)

	for {
		select {
		case t, _ := <-pm.Ticker.C:
			pm.check(t)
		case _, ok := <-pm.done:
			if ok {
				pm.Ticker.Stop()
				close(pm.done)
				pm.done = nil
			}
			return
		}
	}
}

// Stop stops the RequestRunner.
func (pm *RequestRunner) Stop() {
	pm.Ticker.Stop()
	pm.done <- struct{}{}
}

func (pm *RequestRunner) setupTicker(atTime time.Time) <-chan time.Time {
	pm.Ticker = time.NewTicker(pm.UpdateRequest.UpdateInterval)
	pm.check(atTime)
	return pm.Ticker.C
}

func (pm *RequestRunner) check(t time.Time) {
	if pm.LatestCheck.Sub(t) >= 0 {
		return
	}

	fmt.Printf(".")
	srcs := pm.SourceSet.Sources()

	// TODO(jmatt) Actually return the result in a way that might be
	// useful to a client.
	targets := *pm.UpdateRequest.Targets
	for j := range targets {
		for _, src := range srcs {
			pkg, e := src.FetchUpdate(&targets[j])
			if e != nil {
				if e != ErrNoUpdate && e != ErrUnknownPkg {
					fmt.Printf("error getting update: %v\n", e)
				}
				continue
			}
			e = pm.Processor(pkg, src)
			if e != nil {
				fmt.Printf("error processing package %v\n", e)
				continue
			}
			// TODO(jmatt) verify pkg on disk
			targets[j] = *pkg
		}
	}

	// record when the check finishes. If it took longer than the update
	// interval we can use this to avoid doing a flurry of checks once
	// the check completes
	pm.LatestCheck = time.Now()
}
