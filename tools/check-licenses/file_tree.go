// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime/trace"
	"strings"
	"sync"
)

// NewFileTree returns an instance of FileTree, given the input configuration
// file.
func NewFileTree(ctx context.Context, root string, parent *FileTree, config *Config, metrics *Metrics) *FileTree {
	defer trace.StartRegion(ctx, "NewFileTree").End()
	var ft FileTree
	ft.Init()

	abs, _ := filepath.Abs(root)
	ft.Name = filepath.Base(abs)
	ft.Path = abs
	ft.Parent = parent

	for _, customProjectLicense := range config.CustomProjectLicenses {
		if strings.HasSuffix(root, customProjectLicense.ProjectRoot) {
			metrics.increment("num_single_license_files")
			licLocation := filepath.Join(root, customProjectLicense.LicenseLocation)
			ft.SingleLicenseFiles[licLocation] = []*License{}
			break
		}
	}

	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			for _, skipDir := range config.SkipDirs {
				if info.Name() == skipDir || path == skipDir {
					log.Printf("skipping: %s", path)
					return filepath.SkipDir
				}
			}
			if path != root {
				child := NewFileTree(ctx, path, &ft, config, metrics)
				ft.Children[path] = child
				return filepath.SkipDir
			}
		}

		if info.Size() == 0 {
			// An empty file has no content to copyright. Skip.
			return nil
		}
		for _, skipFile := range config.SkipFiles {
			if strings.ToLower(info.Name()) == skipFile || strings.ToLower(path) == skipFile {
				log.Printf("skipping: %s", path)
				return nil
			}
		}
		if hasLowerPrefix(info.Name(), config.SingleLicenseFiles) {
			metrics.increment("num_single_license_files")
			ft.SingleLicenseFiles[path] = []*License{}
			return nil
		}
		if hasExt(info.Name(), config.TextExtensionList) {
			metrics.increment("num_non_single_license_files")
			newFile, err := NewFile(path, &ft)
			if err == nil {
				ft.Files = append(ft.Files, newFile)
			}
		} else {
			log.Printf("ignoring: %s", path)
			metrics.increment("num_extensions_excluded")
		}
		return nil
	})
	if err != nil {
		// TODO(jcecil): This must be an error.
		fmt.Printf("error while traversing directory '%v", err)
		return nil
	}

	return &ft
}

// FileTree is an in memory representation of the state of the repository.
type FileTree struct {
	Name               string                `json:"name"`
	Path               string                `json:"path"`
	SingleLicenseFiles map[string][]*License `json:"project licenses"`
	Files              []*File               `json:"files"`
	Children           map[string]*FileTree  `json:"children"`
	Parent             *FileTree             `json:"-"`

	sync.RWMutex
}

func (license_file_tree *FileTree) Init() {
	license_file_tree.Children = make(map[string]*FileTree)
	license_file_tree.SingleLicenseFiles = make(map[string][]*License)
}

func (file_tree *FileTree) propagateProjectLicenses(config *Config) {
	propagate := true
	for _, dirName := range config.StopLicensePropagation {
		if file_tree.Name == dirName {
			propagate = false
			break
		}
	}

	if propagate && file_tree.Parent != nil {
		for key, val := range file_tree.Parent.SingleLicenseFiles {
			file_tree.SingleLicenseFiles[key] = val
		}
	}

	for _, child := range file_tree.Children {
		child.propagateProjectLicenses(config)
	}
}

func (file_tree *FileTree) getSingleLicenseFileIterator() <-chan *FileTree {
	ch := make(chan *FileTree, 1)
	go func() {
		var curr *FileTree
		var q []*FileTree
		q = append(q, file_tree)
		var pos int
		for len(q) > 0 {
			pos = len(q) - 1
			curr = q[pos]
			q = q[:pos]
			if len(curr.SingleLicenseFiles) > 0 {
				ch <- curr
			}
			curr.RLock()
			for _, child := range curr.Children {
				q = append(q, child)
			}
			curr.RUnlock()
		}
		close(ch)
	}()
	return ch
}

func (file_tree *FileTree) getFileIterator() <-chan *File {
	ch := make(chan *File, 1)
	go func() {
		var curr *FileTree
		var q []*FileTree
		q = append(q, file_tree)
		var pos int
		for len(q) > 0 {
			pos = len(q) - 1
			curr = q[pos]
			q = q[:pos]
			for _, file := range curr.Files {
				ch <- file
			}
			curr.RLock()
			for _, child := range curr.Children {
				q = append(q, child)
			}
			curr.RUnlock()
		}
		close(ch)
	}()
	return ch
}

// Maps are used in FileTree to prevent duplicate values (since go doesn't have sets).
// However, Maps make the final JSON object difficult to read.
// Define a custom MarshalJSON function to convert the internal Maps into slices.
func (file_tree *FileTree) MarshalJSON() ([]byte, error) {
	type Alias FileTree
	childrenList := []*FileTree{}
	fileList := []string{}

	for _, c := range file_tree.Children {
		childrenList = append(childrenList, c)
	}

	for _, f := range file_tree.Files {
		fileList = append(fileList, f.Name)
	}

	return json.Marshal(&struct {
		*Alias
		Files    []string    `json:"files"`
		Children []*FileTree `json:"children"`
	}{
		Alias:    (*Alias)(file_tree),
		Files:    fileList,
		Children: childrenList,
	})
}

func (file_tree *FileTree) saveTreeState(filename string) error {
	jsonString, err := json.MarshalIndent(file_tree, "", " ")
	if err != nil {
		return fmt.Errorf("error marshalling the file tree: %v\n", err)
	}

	file, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer file.Close()
	_, err = io.WriteString(file, string(jsonString))
	if err != nil {
		return err
	}
	return nil
}

// hasExt returns true if path has one of the extensions in the list.
func hasExt(path string, exts []string) bool {
	if ext := filepath.Ext(path); ext != "" {
		ext = ext[1:]
		for _, e := range exts {
			if e == ext {
				return true
			}
		}
	}
	return false
}

// hasLowerPrefix returns true if the name has one of files as a prefix in
// lower case.
func hasLowerPrefix(name string, files []string) bool {
	name = strings.ToLower(name)
	for _, f := range files {
		if strings.HasPrefix(name, f) {
			return true
		}
	}
	return false
}
