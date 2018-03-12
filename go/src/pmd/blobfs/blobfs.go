// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package blobfs provides some wrappers around interactions with the blobfs.
// TODO(raggi): add support for blob garbage collection
package blobfs

import (
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"

	"fuchsia.googlesource.com/merkle"
)

// Manager wraps operations for reading and writing to blobfs, and will later
// tackle more complex problems such as managing reference counting and garbage
// collection of blobs.
type Manager struct {
	Root    string
	tmpDir  string
	channel zx.Channel
}

// New constructs a new Manager for the blobfs mount at the given root.
// tmpDir is a temporary directory where blobs will be temporarily stored if the
// blob key is not known at creation time. tmpDir may be empty, in which case
// the OS default temporary directory is used.
func New(root, tmpDir string) (*Manager, error) {
	if tmpDir == "" {
		tmpDir = os.TempDir()
	}

	rootFDIO, err := syscall.OpenPath(root, 0, 0644)
	if err != nil {
		return nil, fmt.Errorf("pkgfs: blobfs: can't open %q: %s", root, err)
	}
	defer rootFDIO.Close()
	rootIO, ok := rootFDIO.(*fdio.RemoteIO)
	if !ok {
		return nil, fmt.Errorf("pkgfs: blobfs: can't open blobfs root %q with remoteio protocol, got %#v", root, rootFDIO)
	}
	handles, err := rootIO.Clone()
	if err != nil {
		return nil, fmt.Errorf("pkgfs: blobfs: can't clone blobfs root handle: %s", err)
	}
	for _, h := range handles[1:] {
		h.Close()
	}
	channel := zx.Channel(handles[0])

	return &Manager{Root: root, tmpDir: tmpDir, channel: channel}, nil
}

// Create makes a new io for writing to blobfs. If the given root looks
// like a blob root key (is non-empty) then it will be used as the blob key and
// validated on Close - in this case size must also be accurate or Close() will
// fail. If the given root is the empty string, the returned writer will first
// stream to a temporary directory, and compute the blob root on Close. In the
// latter case, the returned writer will also implement the Rooter interface,
// and the root can be read after a successful Close().
func (m *Manager) Create(root string, size int64) (io.WriteCloser, error) {
	if root != "" {
		f, err := os.Create(m.bpath(root))
		if err != nil {
			return nil, err
		}
		f.Truncate(size)
		return f, nil
	}

	return newTempProxy(m)
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

type tempProxy struct {
	f    *os.File
	m    *Manager
	root []byte
}

func newTempProxy(m *Manager) (io.WriteCloser, error) {
	f, err := ioutil.TempFile(m.tmpDir, "blob-proxy")
	if err != nil {
		return nil, err
	}
	// unlink the file immediately so that once we've closed it, it is removed
	os.Remove(f.Name())
	return &tempProxy{f: f, m: m}, nil
}

func (t *tempProxy) Close() error {
	defer t.f.Close()

	if _, err := t.f.Seek(0, io.SeekStart); err != nil {
		return err
	}

	// TODO(raggi): stream the data to the merkle tree on each write instead?
	var tree merkle.Tree
	if _, err := tree.ReadFrom(t.f); err != nil {
		return err
	}

	t.root = tree.Root()

	rootHex := fmt.Sprintf("%x", t.root)
	// if we already have the blob, just continue "as normal"
	if t.m.HasBlob(rootHex) {
		return nil
	}

	if _, err := t.f.Seek(0, io.SeekStart); err != nil {
		return err
	}

	info, err := t.f.Stat()
	if err != nil {
		return err
	}

	w, err := t.m.Create(rootHex, info.Size())
	if err != nil {
		return err
	}
	_, err = io.Copy(w, t.f)

	err2 := w.Close()

	if err == nil {
		return err2
	}
	return err
}

func (t *tempProxy) Write(p []byte) (int, error) {
	return t.f.Write(p)
}

func (t *tempProxy) Root() (string, error) {
	if t.root == nil {
		return "", os.ErrInvalid
	}
	return fmt.Sprintf("%x", t.root), nil
}

// Rooter is an interface that indicates the object provides blobfs merkle
// roots that identify it's content
type Rooter interface {
	Root() (string, error)
}
