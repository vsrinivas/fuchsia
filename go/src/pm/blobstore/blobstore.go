// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package blobstore provides some wrappers around interactions with the blobstore.
// TODO(raggi): add support for blob garbage collection
package blobstore

import (
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/merkle"
)

// Manager wraps operations for reading and writing to blobstore, and will later
// tackle more complex problems such as managing reference counting and garbage
// collection of blobs.
type Manager struct {
	root   string
	tmpDir string
}

// New constructs a new Manager for the blobstore mount at the given root.
// tmpDir is a temporary directory where blobs will be temporarily stored if the
// blob key is not known at creation time. tmpDir may be empty, in which case
// the OS default temporary directory is used.
func New(root, tmpDir string) (*Manager, error) {
	if tmpDir == "" {
		tmpDir = os.TempDir()
	}
	return &Manager{root: root, tmpDir: tmpDir}, nil
}

// Create makes a new io for writing to the blobstore. If the given root looks
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

// Open opens a blobstore blob for reading
func (m *Manager) Open(root string) (*os.File, error) {
	return os.Open(m.bpath(root))
}

// HasBlob returns true if the requested blob is available, false otherwise
func (m *Manager) HasBlob(root string) bool {
	_, err := os.Stat(m.bpath(root))
	return err == nil
}

func (m *Manager) bpath(root string) string {
	return filepath.Join(m.root, root)
}

type tempProxy struct {
	f    *os.File
	m    *Manager
	root []byte
}

func newTempProxy(m *Manager) (io.WriteCloser, error) {
	f, err := ioutil.TempFile(m.tmpDir, "blobstore-proxy")
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

// Rooter is an interface that indicates the object provides blobstore merkle
// roots that identify it's content
type Rooter interface {
	Root() (string, error)
}
