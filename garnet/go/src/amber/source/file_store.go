// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"

	"github.com/flynn/go-tuf/client"
)

// FileStore implements go-tuf/client.LocalStore in terms of a JSON file on the
// local filesystem.
type FileStore struct {
	Path string

	// mu serializes file read/write operations against this store
	mu sync.Mutex
}

var _ client.LocalStore = &FileStore{}

func NewFileStore(path string) (*FileStore, error) {
	s := &FileStore{Path: path, mu: sync.Mutex{}}

	return s, os.MkdirAll(filepath.Dir(s.Path), 0755)
}

func (s *FileStore) GetMeta() (map[string]json.RawMessage, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	return s.getMetaLocked()
}

func (s *FileStore) getMetaLocked() (map[string]json.RawMessage, error) {
	var m = make(map[string]json.RawMessage)

	f, err := os.Open(s.Path)
	if err != nil {
		if os.IsNotExist(err) {
			return m, nil
		}
		return m, err
	}
	defer f.Close()
	return m, json.NewDecoder(f).Decode(&m)
}

func (s *FileStore) SetMeta(name string, meta json.RawMessage) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	m, err := s.getMetaLocked()
	if err != nil {
		return err
	}

	m[name] = meta

	newPath := s.Path + ".new"

	f, err := os.Create(newPath)
	if err != nil {
		return err
	}

	if err := json.NewEncoder(f).Encode(&m); err != nil {
		f.Close()
		os.Remove(newPath)
		return err
	}

	if err := f.Close(); err != nil {
		os.Remove(newPath)
		return err
	}

	return os.Rename(newPath, s.Path)
}
