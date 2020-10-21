// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package v2

import (
	"fmt"
	"path/filepath"
	"sync"
)

type File struct {
	Path    string
	Symlink string
}

func NewFile(path string) (*File, error) {
	if f := globalFileMap.retrieveFileIfExists(path); f != nil {
		return f, nil
	}

	symlink, err := filepath.EvalSymlinks(path)
	if err != nil {
		return nil, fmt.Errorf("error creating file %v: %v\n", path, err)
	}
	file := &File{
		Symlink: symlink,
		Path:    path,
	}
	return globalFileMap.insertFileIfMissing(file), nil
}

// ============================================================================
// Maintain a global map of File objects to ensure we don't accidentally
// process the same file multiple times, which may happen during dependency
// traversal.

type fileMap struct {
	internal map[string]*File
	sync.RWMutex
}

var globalFileMap = fileMap{internal: make(map[string]*File)}

func (f *fileMap) retrieveFileIfExists(path string) *File {
	f.RLock()
	defer f.RUnlock()
	return f.internal[path]
}

func (f *fileMap) insertFileIfMissing(file *File) *File {
	f.Lock()
	defer f.Unlock()
	if val, ok := f.internal[file.Path]; ok {
		// TODO: merge file info into val before returning.
		return val
	}
	f.internal[file.Path] = file
	return file
}
