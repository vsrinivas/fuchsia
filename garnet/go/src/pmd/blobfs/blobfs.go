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
	f, err := m.OpenFile(root, os.O_WRONLY|os.O_APPEND, 0777)
	switch err := err.(type) {
	case nil:
		f.Close()

		// if there was no error, then we opened the file for writing and the file was
		// writable, which means it exists and is being written by someone.
		return false
	case *zx.Error:
		switch err.Status {
		case zx.ErrAccessDenied:
			// Access denied indicates we explicitly know that we have opened a blob that
			// already exists and it is not writable - meaning it's already written.
			return true
		case zx.ErrNotFound:
			return false
		}
	}

	log.Printf("blobfs: unknown error asserting blob existence: %s", err)
	return false
}

func (m *Manager) Blobs() ([]string, error) {
	d, err := m.OpenFile(".", syscall.O_DIRECTORY, 0777)
	if err != nil {
		return nil, err
	}
	defer d.Close()
	return d.Readdirnames(-1)
}
