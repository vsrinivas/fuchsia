// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package msdosfs

import (
	"sync"
	"time"

	"fuchsia.googlesource.com/thinfs/lib/fs"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/node"
)

type directory struct {
	fs    *fsFAT
	node  node.DirectoryNode
	flags fs.OpenFlags // Original flags which opened this directory

	sync.RWMutex
	closed bool
}

// Ensure the directory implements the fs.Directory interface
var _ fs.Directory = (*directory)(nil)

func (d *directory) Close() error {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return fs.ErrUnmounted
	}
	d.Lock()
	defer d.Unlock()
	if d.closed {
		return fs.ErrNotOpen
	}
	d.closed = true

	return closeDirectory(d.node, false)
}

func (d *directory) Stat() (int64, time.Time, time.Time, error) {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return 0, time.Time{}, time.Time{}, fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return 0, time.Time{}, time.Time{}, fs.ErrNotOpen
	}

	return stat(d.node)
}

func (d *directory) Touch(lastAccess, lastModified time.Time) error {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return fs.ErrNotOpen
	}

	touch(d.node, lastAccess, lastModified)
	return nil
}

func (d *directory) Dup() (fs.Directory, error) {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return nil, fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return nil, fs.ErrNotOpen
	}

	dup(d.node)

	return &directory{
		fs:    d.fs,
		node:  d.node,
		flags: d.flags,
	}, nil
}

func (d *directory) Reopen(flags fs.OpenFlags) (fs.Directory, error) {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return nil, fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return nil, fs.ErrNotOpen
	}

	if (flags.Read() && !d.flags.Read()) || (flags.Write() && !d.flags.Write()) {
		return nil, fs.ErrPermission
	} else if flags.File() {
		return nil, fs.ErrNotAFile
	}

	dup(d.node)
	return &directory{
		fs:    d.fs,
		node:  d.node,
		flags: d.flags,
	}, nil
}

func (d *directory) Read() ([]fs.Dirent, error) {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return nil, fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return nil, fs.ErrNotOpen
	}

	return readDir(d.node)
}

func (d *directory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return nil, nil, fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return nil, nil, fs.ErrNotOpen
	}

	if (flags.Write() || flags.Create()) && d.fs.info.Readonly {
		return nil, nil, fs.ErrPermission
	} else if flags.Create() && !d.flags.Write() {
		return nil, nil, fs.ErrPermission // Creation requires the parent directory to be writable
	} else if !flags.Read() && !flags.Write() {
		return nil, nil, fs.ErrPermission // Cannot open a file with no permissions
	}

	n, err := open(d.node, name, flags)
	if err != nil {
		return nil, nil, err
	}

	if n.IsDirectory() {
		return nil, &directory{
			fs:    d.fs,
			node:  n.(node.DirectoryNode),
			flags: flags | fs.OpenFlagWrite | fs.OpenFlagRead,
		}, nil
	}
	return &file{
		fs:       d.fs,
		node:     n.(node.FileNode),
		flags:    flags,
		position: &filePosition{},
	}, nil, nil
}

func (d *directory) Rename(src, dst string) error {
	d.fs.Lock()
	defer d.fs.Unlock()
	if d.fs.unmounted {
		return fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return fs.ErrNotOpen
	}

	return rename(d.node, src, dst)
}

func (d *directory) Flush() error {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return fs.ErrNotOpen
	}

	return flush(d.node)
}

func (d *directory) Unlink(target string) error {
	d.fs.RLock()
	defer d.fs.RUnlock()
	if d.fs.unmounted {
		return fs.ErrUnmounted
	}
	d.RLock()
	defer d.RUnlock()
	if d.closed {
		return fs.ErrNotOpen
	}

	return unlink(d.node, target)
}
