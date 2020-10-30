// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"encoding/json"
	"fmt"
	"path/filepath"
	"sync"
)

type File struct {
	Name     string
	Path     string     `json:"-"`
	Symlink  string     `json:"-"`
	Parent   *FileTree  `json:"-"`
	Licenses []*License `json:"licenses"`
}

func NewFile(path string, parent *FileTree) (*File, error) {
	if f := globalFileMap.retrieveFileIfExists(path); f != nil {
		return f, nil
	}

	symlink, err := filepath.EvalSymlinks(path)
	if err != nil {
		return nil, fmt.Errorf("error creating file %v: %v\n", path, err)
	}
	file := &File{
		Name:    filepath.Base(path),
		Path:    path,
		Symlink: symlink,
		Parent:  parent,
	}
	return globalFileMap.insertFileIfMissing(file), nil
}

// Use a custom Marshal function to make Files easier to read in JSON:
// reduce associated license information down to a string list.
func (f *File) MarshalJSON() ([]byte, error) {
	type Alias File
	licenseList := []string{}

	for _, l := range f.Licenses {
		licenseList = append(licenseList, l.Category)
	}

	return json.Marshal(&struct {
		*Alias
		Licenses []string `json:"licenses"`
	}{
		Alias:    (*Alias)(f),
		Licenses: licenseList,
	})
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
