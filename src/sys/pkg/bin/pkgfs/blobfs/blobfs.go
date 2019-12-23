// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package blobfs provides some wrappers around interactions with the blobfs.
// TODO(raggi): add support for blob garbage collection
package blobfs

import (
	"log"
	"os"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/io"
	"syscall/zx/zxwait"

	"fuchsia.googlesource.com/pmd/iou"
)

// Manager wraps operations for reading and writing to blobfs, and will later
// tackle more complex problems such as managing reference counting and garbage
// collection of blobs.
type Manager struct {
	dir *fdio.Directory
}

// New constructs a new Manager for the blobfs mount at the given root.
func New(blobDir *fdio.Directory) (*Manager, error) {
	return &Manager{blobDir}, nil
}

// Open opens a blobfs blob for reading
func (m *Manager) Open(root string) (*os.File, error) {
	return m.OpenFile(root, os.O_RDONLY, 0777)
}

// OpenFile opens a blobfs path with the given flags
func (m *Manager) OpenFile(root string, flags int, mode uint32) (*os.File, error) {
	return iou.OpenFrom(m.dir, root, flags, mode)
}

// Channel returns an the FDIO directory handle for the blobfs root
func (m *Manager) Channel() zx.Channel {
	return zx.Channel(m.dir.Handles()[0])
}

// HasBlob returns true if the requested blob is available for reading, false otherwise
func (m *Manager) HasBlob(root string) bool {
	f, err := m.dir.Open(root, io.OpenRightReadable|io.OpenFlagNotDirectory, io.ModeTypeFile)
	if err != nil {
		// if the blob can't be opened for read at all, it doesn't
		// exist and isn't in the process of being written.
		return false
	}
	defer f.Close()

	file, ok := f.(*fdio.File)
	if !ok {
		// a node exists with the name, but it isn't a file.
		return false
	}

	observed, err := zxwait.Wait(zx.Handle(file.Event), zx.SignalUser0, 0)

	// The above wait should either succeed or fail with zx.ErrTimedOut.
	// Log unexpected error conditions.
	if err != nil {
		e, ok := err.(*zx.Error)
		if !ok || e.Status != zx.ErrTimedOut {
			log.Printf("blobfs: unknown error asserting blob existence: %s", err)
		}
	}

	// Blobfs will allow blobs that are in the process of being written to
	// be opened for read, and it will set zx.SignalUser0 on the blob's
	// event when it actually becomes readable. For the purposes of pkgfs,
	// we only have a blob if it exists and is readable.
	return err == nil && (observed&zx.SignalUser0) == zx.SignalUser0
}

func (m *Manager) Blobs() ([]string, error) {
	d, err := m.OpenFile(".", syscall.O_DIRECTORY, 0777)
	if err != nil {
		return nil, err
	}
	defer d.Close()
	return d.Readdirnames(-1)
}
