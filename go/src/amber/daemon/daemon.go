// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"errors"
	"sync"
	"time"
)

var ErrReqNotFound = errors.New("amber/daemon: no corresponding request found")

// Deamon provides access to a set of Sources and oversees the polling of those
// Sources.
//
// Note that methods on this struct are not designed for parallel access.
// Execution contexts sharing a single Daemon instance should mediate access
// to all calls into the Daemon.
type Daemon struct {
	runners   map[UpdateRequest]*RequestRunner
	srcs      *SourceSet
	stopCount sync.WaitGroup
}

// NewDaemon creates a Daemon with the given SourceSet
func NewDaemon(s *SourceSet) *Daemon {
	return &Daemon{srcs: s,
		runners: make(map[UpdateRequest]*RequestRunner)}
}

// AddRequest starts monitoring for updates to the UpdateRequest supplied. This
// monitoring can be canceled with a call to CancelRequest or CancelAll
func (d *Daemon) AddRequest(req *UpdateRequest) {
	rec := RequestRunner{
		UpdateRequest: req,
		LatestCheck:   time.Now().Add(-2 * req.UpdateInterval),
		SourceSet:     d.srcs,
	}

	d.runners[*req] = &rec
	go func() {
		defer d.stopCount.Done()
		rec.Run()
	}()
}

// CancelRequest stops monitoring for updates described by a UpdateRequest
// previously add by a call to AddRequest. The method waits for the monitoring
// routine to stop before returning and therefore this method may block.
func (d *Daemon) CancelRequest(req *UpdateRequest) error {
	r := d.runners[*req]
	if r == nil {
		return ErrReqNotFound
	}
	r.Stop()
	d.stopCount.Add(1)
	delete(d.runners, *req)
	return nil
}

// CancelAll cancels all currently monitored UpdateRequests that were previously
// added to this Daemon with a call to AddRequest. This method blocks until all
// runners have stopped.
func (d *Daemon) CancelAll() {
	for _, v := range d.runners {
		d.stopCount.Add(1)
		v.Stop()
	}

	d.stopCount.Wait()
	d.runners = make(map[UpdateRequest]*RequestRunner)
}
