// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

// DaemonProvider wraps access to a Daemon. Daemon() will block until
// SetDaemon() has been called at least once.
type DaemonProvider struct {
	d *Daemon
	c chan struct{}
}

func NewDaemonProvider() *DaemonProvider {
	return &DaemonProvider{c: make(chan struct{})}
}

func (dp *DaemonProvider) Daemon() *Daemon {
	<-dp.c
	return dp.d
}

func (dp *DaemonProvider) SetDaemon(d *Daemon) {
	dp.d = d
	close(dp.c)
}
