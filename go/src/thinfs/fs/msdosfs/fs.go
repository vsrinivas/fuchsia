// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package msdosfs implements the FAT filesystem
package msdosfs

import (
	"errors"
	"sync"
	"time"

	"fuchsia.googlesource.com/thinfs/block"
	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/cluster"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/direntry"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/node"
	"fuchsia.googlesource.com/thinfs/thinio"
	"github.com/golang/glog"
)

const (
	defaultCacheSize = 8 * 1024 * 1024
)

type fsFAT struct {
	opts fs.FileSystemOptions // FS-independent options passed to filesystem
	info *node.Metadata
	root node.DirectoryNode

	// This global lock is only acquired as 'writable' during two operations:
	// 1) Renaming
	// 2) Unmounting
	sync.RWMutex
	unmounted bool
}

// Ensure the fsFAT implements the fs.FileSystem interface
var _ fs.FileSystem = (*fsFAT)(nil)

// New returns a new FAT filesystem.
func New(path string, dev block.Device, opts fs.FileSystemOptions) (fs.FileSystem, error) {
	glog.V(0).Infof("Creating a new FAT fs at %s\n", path)
	f := fsFAT{
		opts: opts,
		info: &node.Metadata{
			Dev:      thinio.NewConductor(dev, defaultCacheSize),
			Readonly: (opts & fs.ReadOnly) != 0,
		},
	}
	f.info.Init()

	var err error
	// Load and validate the bootrecord (superblock-like FAT structure).
	f.info.Br, err = bootrecord.New(f.info.Dev)
	if err != nil {
		return nil, err
	}

	f.info.ClusterMgr, err = cluster.Mount(f.info.Dev, f.info.Br, f.info.Readonly)
	if err != nil {
		return nil, err
	}

	switch f.info.Br.Type() {
	case bootrecord.FAT32:
		startCluster := f.info.Br.RootCluster()
		r, err := node.NewDirectory(f.info, startCluster, time.Time{})
		if err != nil {
			return nil, err
		}
		r.SetSize(int64(f.info.Br.ClusterSize()) * int64(r.NumClusters()))
		f.root = r
	case bootrecord.FAT16, bootrecord.FAT12:
		offsetStart, numRootEntriesMax := f.info.Br.RootReservedInfo()
		f.root = node.NewRoot(f.info, offsetStart, numRootEntriesMax*direntry.DirentrySize)
	default:
		panic("Unexpected FAT type")
	}
	f.root.RefUp()
	f.info.Dcache.Insert(f.root.ID(), f.root)

	return &f, nil
}

// Close unmounts the FAT filesystem.
func (f *fsFAT) Close() error {
	glog.V(0).Info("Closing filesystem")
	f.Lock()
	defer f.Unlock()
	if f.unmounted {
		return errors.New("Already unmounted")
	}

	f.unmounted = true
	if !f.info.Readonly {
		// For writeable filesystems, close all files so their sizes are updated. Additionally, sync
		// the block device.
		directoryNodes := f.info.Dcache.AllEntries()
		for _, parent := range directoryNodes {
			children := parent.ChildFiles()
			for _, child := range children {
				_, index := child.Parent()
				cluster := child.StartCluster()
				mTime := child.MTime()
				size := uint32(child.Size())
				if _, err := node.Update(parent, cluster, mTime, size, index); err != nil {
					panic(err)
				}
			}
		}
		if err := f.info.Dev.Flush(); err != nil {
			panic(err)
		}
	}
	return f.info.ClusterMgr.Unmount()
}

// RootDirectory returns the root directory.
func (f *fsFAT) RootDirectory() fs.Directory {
	glog.V(0).Info("Getting root directory")
	f.RLock()
	defer f.RUnlock()
	if f.unmounted {
		return nil
	}

	f.root.RefUp()
	f.info.Dcache.Acquire(f.root.ID())

	flags := fs.OpenFlagRead | fs.OpenFlagDirectory
	if !f.info.Readonly {
		flags |= fs.OpenFlagWrite
	}

	rootDir := &directory{
		fs:    f,
		flags: flags,
		node:  f.root,
	}
	return rootDir
}

// Blockcount returns the total ("used" + "free") number of blocks on the FAT filesystem.
func (f *fsFAT) Blockcount() int64 {
	return int64(f.info.Br.NumUsableClusters())
}

// Blocksize returns the size of a single cluster in the FAT filesystem.
func (f *fsFAT) Blocksize() int64 {
	return int64(f.info.Br.ClusterSize())
}

// Size returns the capacity (in bytes) of the filesystem.
func (f *fsFAT) Size() int64 {
	return f.Blockcount() * f.Blocksize()
}
