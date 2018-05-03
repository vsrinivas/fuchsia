// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package blobfs provides some wrappers around interactions with the blobfs.
// TODO(raggi): add support for blob garbage collection
package blobfs

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"syscall"
	"syscall/zx"
)

// Manager wraps operations for reading and writing to blobfs, and will later
// tackle more complex problems such as managing reference counting and garbage
// collection of blobs.
type Manager struct {
	Root    string
	channel zx.Channel
}

// New constructs a new Manager for the blobfs mount at the given root.
func New(root string) (*Manager, error) {
	rootFDIO, err := syscall.OpenPath(root, 0, 0644)
	if err != nil {
		return nil, fmt.Errorf("pkgfs: blobfs: can't open %q: %s", root, err)
	}
	defer rootFDIO.Close()
	rootIO, err := rootFDIO.Clone()
	if err != nil {
		return nil, fmt.Errorf("pkgfs: blobfs: can't clone blobfs root handle: %s", err)
	}
	handles := rootIO.Handles()
	for _, h := range handles[1:] {
		h.Close()
	}
	// The first handle is always a channel.
	channel := zx.Channel(handles[0])

	return &Manager{Root: root, channel: channel}, nil
}

// Open opens a blobfs blob for reading
func (m *Manager) Open(root string) (*os.File, error) {
	return os.Open(m.bpath(root))
}

// Channel returns an the FDIO directory handle for the blobfs root
func (m *Manager) Channel() zx.Channel {
	return m.channel
}

// HasBlob returns true if the requested blob is available for reading, false otherwise
func (m *Manager) HasBlob(root string) bool {
	// blobfs currently provides a signal handle over handle 2 in remoteio, but
	// if it ever moves away from remoteio, or there are ever protocol or api
	// changes this path is very brittle. There's no hard requirement for
	// intermediate clients/servers to retain this handle. As such we disregard
	// that option for detecting readable state.
	// blobfs also does not reject open flags on in-flight blobs that only
	// contain "read", even though reads on those files would return errors. This
	// could be used to detect blob readability state, by opening and then issueing
	// a read, but that will make blobfs do a lot of work.
	// the final method chosen here for now is to open the blob for writing,
	// non-exclusively. That will be rejected if the blob has already been fully
	// written.
	f, err := syscall.Open(m.bpath(root), syscall.O_WRONLY|syscall.O_APPEND, 0)
	if os.IsNotExist(err) {
		return false
	}
	syscall.Close(f)

	// if there was no error, then we opened the file for writing and the file was
	// writable, which means it exists and is being written by someone.
	if err == nil {
		return false
	}

	// Access denied indicates we explicitly know that we have opened a blob that
	// already exists and it is not writable - meaning it's already written.
	if e, ok := err.(zx.Error); ok && e.Status == zx.ErrAccessDenied {
		return true
	}

	log.Printf("blobfs: unknown error asserting blob existence: %s", err)

	// fall back to trying to stat the file. note that stat doesn't tell us if the
	// file is readable/writable, but we best assume here that if stat succeeds
	// there's something blocking the write path, and there's a good chance that
	// the file exists and is readable. this could lead to premature package
	// activation and associated errors, but those are hopefully better than some
	// problems the other way around, such as repeatedly trying to re-download
	// blobs. if the stat does not succeed however, we probably want to try to
	// indicate to various systems that they should try to fetch/write the blob,
	// hopefully leading to write-repair.
	if _, err := os.Stat(m.bpath(root)); err == nil {
		return true
	}
	return false
}

func (m *Manager) bpath(root string) string {
	return filepath.Join(m.Root, root)
}

func (m *Manager) PathOf(root string) string {
	return m.bpath(root)
}
