// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package metadata contains the important info / device controlling pointers which are shared
// across the FAT implementation.
package metadata

import (
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/cluster"
	"fuchsia.googlesource.com/thinfs/lib/thinio"
)

// Info describes the important, shared metadata of the filesystem.
type Info struct {
	Dev        *thinio.Conductor      // Access to device (with cache).
	Br         *bootrecord.Bootrecord // Superblock of filesystem
	ClusterMgr *cluster.Manager
	Readonly   bool
}
