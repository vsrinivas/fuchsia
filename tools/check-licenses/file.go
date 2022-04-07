// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"encoding/json"
	"fmt"
	"path/filepath"
)

type File struct {
	Name     string
	Path     string     `json:"path"`
	Symlink  string     `json:"symlink"`
	Parent   *FileTree  `json:"-"`
	Licenses []*License `json:"licenses"`
}

// fileByPath implements sort.Interface for []*File based on the Path field.
type fileByPath []*File

func (a fileByPath) Len() int           { return len(a) }
func (a fileByPath) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a fileByPath) Less(i, j int) bool { return a[i].Path < a[j].Path }

func NewFile(path string, parent *FileTree) (*File, error) {
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
	return file, nil
}

// shouldProcess looks at this file extension and the extensions lists in the config file
// to determine whether or not this file should be processed by checklicenses.
func (f *File) shouldProcess(strict bool, config *Config) bool {
	return hasExt(f.Name, config.StrictTextExtensionList) || (!strict && hasExt(f.Name, config.TextExtensionList))
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

func (f *File) String() string {
	return f.Path
}
