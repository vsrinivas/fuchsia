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
	return &File{
		Name: filepath.Base(path),
		Path: path,
		Data: data,
	}, nil
}
