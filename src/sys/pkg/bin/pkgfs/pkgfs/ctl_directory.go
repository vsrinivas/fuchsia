// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"log"
	"strings"
	"sync"
	"thinfs/fs"
	"time"
)

type ctlDirectory struct {
	unsupportedDirectory
	fs   *Filesystem
	mu   sync.RWMutex
	dirs map[string]fs.Directory
}

func (d *ctlDirectory) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *ctlDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.SplitN(name, "/", 2)

	d.mu.RLock()
	subdir, ok := d.dirs[parts[0]]
	d.mu.RUnlock()
	if !ok {
		return nil, nil, nil, fs.ErrNotFound
	}

	if len(parts) == 1 {
		return nil, subdir, nil, nil
	}

	return subdir.Open(parts[1], flags)
}

func (d *ctlDirectory) Read() ([]fs.Dirent, error) {

	d.mu.RLock()
	dirs := make([]fs.Dirent, 0, len(d.dirs))
	for n := range d.dirs {
		dirs = append(dirs, dirDirEnt(n))
	}
	d.mu.RUnlock()
	return dirs, nil
}

func (d *ctlDirectory) Close() error {
	return nil
}

func (d *ctlDirectory) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *ctlDirectory) Unlink(path string) error {
	// the "garbage" file is a special control file. When it is unlinked,
	// we trigger garbage collection.
	if path == "garbage" {
		if err := d.fs.GC(); err != nil {
			log.Printf("unlink garbage: %s", err)
		}
		return nil
	}

	return d.unsupportedDirectory.Unlink(path)
}
