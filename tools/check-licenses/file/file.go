// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

import (
	"path/filepath"
)

// File is a data struct used to hold the path and text content
// of a file in the fuchsia tree.
type File struct {
	Name string
	Path string `json:"path"`
	Data []*FileData
}

// Order implements sort.Interface for []*File based on the Path field.
type Order []*File

func (a Order) Len() int           { return len(a) }
func (a Order) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a Order) Less(i, j int) bool { return a[i].Path < a[j].Path }

// NewFile returns a new File struct, with the file content loaded
// in.
//
// TODO(jcecil): Consider making the "load" process lazy, to use
// less memory during execution.
func NewFile(path string, ft FileType) (*File, error) {
	// If this file was already created, return the previous File object.
	if f, ok := AllFiles[path]; ok {
		plusVal(RepeatedFileTraversal, path)
		return f, nil
	}

	data, err := NewFileData(path, ft)
	if err != nil {
		return nil, err
	}

	plusVal(NumFiles, path)
	f := &File{
		Name: filepath.Base(path),
		Path: path,
		Data: data,
	}

	AllFiles[path] = f
	return f, nil
}
