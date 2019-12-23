// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"os"

	"fidl/fuchsia/pkg"

	"amber/source"
)

const (
	PackageGarbageDir = "/pkgfs/ctl/garbage"
)

type Daemon struct{}

// NewDaemon creates a Daemon
func NewDaemon() (*Daemon, error) {
	d := &Daemon{}

	return d, nil
}

func (d *Daemon) GC() error {
	// Garbage collection is done by trying to unlink a particular control
	// file exposed by pkgfs.
	return os.Remove(PackageGarbageDir)
}

func (d *Daemon) OpenRepository(config *pkg.RepositoryConfig) (source.Repository, error) {
	return source.OpenRepository(config)
}
