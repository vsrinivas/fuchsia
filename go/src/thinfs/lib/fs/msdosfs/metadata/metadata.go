// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
