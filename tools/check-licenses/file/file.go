// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

import (
	"os"
	"path/filepath"
)

// File is a data struct used to hold the path and text content
// of a file in the fuchsia tree.
type File struct {
	Name    string
	AbsPath string `json:"absPath"`
	RelPath string `json:"relPath"`
	URL     string `json:"url"`
	Data    []*FileData
	Text    []byte
}

// Order implements sort.Interface for []*File based on the AbsPath field.
type Order []*File

func (a Order) Len() int           { return len(a) }
func (a Order) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a Order) Less(i, j int) bool { return a[i].AbsPath < a[j].AbsPath }

// NewFile returns a new File struct, with the file content loaded in.
func NewFile(path string, ft FileType) (*File, error) {
	// If this file was already created, return the previous File object.
	if f, ok := AllFiles[path]; ok {
		plusVal(RepeatedFileTraversal, path)
		return f, nil
	}

	var content []byte
	var err error
	var readFile bool

	relPath := path
	if filepath.IsAbs(path) {
		if relPath, err = filepath.Rel(Config.FuchsiaDir, path); err != nil {
			return nil, err
		}
	}

	absPath := path
	if absPath, err = filepath.Abs(path); err != nil {
		return nil, err
	}

	content = make([]byte, 0)
	if ft == Any {
		// Don't read in regular files.
	} else if ft == CopyrightHeader {
		_, readFile = Config.Extensions[filepath.Ext(path)]
	} else {
		readFile = true
	}

	if readFile {
		content, err = os.ReadFile(path)
		if err != nil {
			return nil, err
		}
		if ft == CopyrightHeader && len(content) > 0 {
			content = content[:min(Config.CopyrightSize, len(content))]
		}
	}

	data, err := NewFileData(absPath, relPath, content, ft)
	if err != nil {
		return nil, err
	}

	plusVal(NumFiles, path)
	if Config.Extensions[filepath.Ext(path)] {
		plusVal(NumPotentialLicenseFiles, path)
	}

	f := &File{
		Name:    filepath.Base(path),
		AbsPath: absPath,
		RelPath: relPath,
		Data:    data,
		Text:    content,
	}

	AllFiles[path] = f
	return f, nil
}

func (f *File) UpdateURLs(projectName string, projectURL string) {
	for _, d := range f.Data {
		d.UpdateURLs(projectName, projectURL)
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
