// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

import (
	"io/ioutil"
	"path/filepath"
)

// File is a data struct used to hold the path and text content
// of a file in the fuchsia tree.
type File struct {
	Name string
	Path string `json:"path"`
	Url  string `json:"url"`
	Data []*FileData
	Text []byte
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

	var content []byte
	var err error
	var readFile bool

	content = make([]byte, 0)
	if ft == Any {
		// Don't read in regular files.
	} else if ft == CopyrightHeader {
		_, readFile = Config.Extensions[filepath.Ext(path)]
	} else {
		readFile = true
	}

	if readFile {
		content, err = ioutil.ReadFile(path)
		if err != nil {
			return nil, err
		}
		if ft == CopyrightHeader && len(content) > 0 {
			content = content[:min(Config.CopyrightSize, len(content))]
		}
	}

	data, err := NewFileData(path, content, ft)
	if err != nil {
		return nil, err
	}

	plusVal(NumFiles, path)
	if Config.Extensions[filepath.Ext(path)] {
		plusVal(NumPotentialLicenseFiles, path)
	}
	f := &File{
		Name: filepath.Base(path),
		Path: path,
		Data: data,
		Text: content,
	}

	AllFiles[path] = f
	return f, nil
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
