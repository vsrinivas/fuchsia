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
func NewFileTree(ctx context.Context, config *Config, metrics *Metrics) *FileTree {
	defer trace.StartRegion(ctx, "NewFileTree").End()
	var ft FileTree
	ft.Init()

	ft.Name = config.BaseDir
	abs, _ := filepath.Abs(config.BaseDir)
	ft.Path = abs

	err := filepath.Walk(config.BaseDir, func(path string, info os.FileInfo, err error) error {
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
			for _, customProjectLicense := range config.CustomProjectLicenses {
				if path == customProjectLicense.ProjectRoot {
					metrics.increment("num_single_license_files")
					// TODO(omerlevran): Fix the directory and file_root having to repeat
					// a directory.
					ft.addSingleLicenseFile(path, customProjectLicense.LicenseLocation)
					break
				}
			}
			return nil
		}

		if info.Size() == 0 {
			// An empty file has no content to copyright. Skip.
			return nil
		}
		for _, skipFile := range config.SkipFiles {
			if strings.ToLower(info.Name()) == skipFile {
				log.Printf("skipping: %s", path)
				return nil
			}
		}
		if hasLowerPrefix(info.Name(), config.SingleLicenseFiles) {
			metrics.increment("num_single_license_files")
			ft.addSingleLicenseFile(path, filepath.Base(path))
			return nil
		}
		if hasExt(info.Name(), config.TextExtensionList) {
			metrics.increment("num_non_single_license_files")
			ft.addFile(path)
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
	Files              []string              `json:"files"`
	Children           map[string]*FileTree  `json:"children"`
	Parent             *FileTree             `json:"-"`

	sync.RWMutex
}

func (license_file_tree *FileTree) Init() {
	license_file_tree.Children = make(map[string]*FileTree)
	license_file_tree.SingleLicenseFiles = make(map[string][]*License)
}

func (file_tree *FileTree) getSetCurr(path string) *FileTree {
	children := strings.Split(filepath.Dir(path), "/")
	curr := file_tree
	file_tree.Lock()
	prefix := ""
	for _, child := range children {
		prefix = filepath.Join(prefix, child)
		if _, found := curr.Children[child]; !found {
			curr.Children[child] = &FileTree{
				Name:   child,
				Parent: curr,
				Path:   prefix,
			}
			curr.Children[child].Init()
		}
		curr = curr.Children[child]
	}
	file_tree.Unlock()
	return curr
}

func (file_tree *FileTree) addFile(path string) {
	curr := file_tree.getSetCurr(path)
	curr.Files = append(curr.Files, filepath.Base(path))
}

func (file_tree *FileTree) addSingleLicenseFile(path string, base string) {
	curr := file_tree.getSetCurr(path)
	curr.SingleLicenseFiles[base] = []*License{}
}

func (file_tree *FileTree) getProjectLicense(path string) *FileTree {
	curr := file_tree
	var gold *FileTree
	pieces := strings.Split(filepath.Dir(path), "/")
	for _, piece := range pieces {
		if len(curr.SingleLicenseFiles) > 0 {
			gold = curr
		}
		curr.RLock()
		if _, found := curr.Children[piece]; !found {
			curr.RUnlock()
			break
		}
		currNext := curr.Children[piece]
		curr.RUnlock()
		curr = currNext
	}
	if len(pieces) > 1 && len(curr.SingleLicenseFiles) > 0 {
		gold = curr
	}
	return gold
}

func (file_tree *FileTree) getPath() string {
	var arr []string
	curr := file_tree
	for {
		if curr == nil {
			break
		}
		arr = append(arr, curr.Name)
		curr = curr.Parent
	}
	var sb strings.Builder
	for i := len(arr) - 1; i >= 0; i-- {
		if len(arr[i]) == 0 {
			continue
		}
		fmt.Fprintf(&sb, "%s/", arr[i])
	}
	return sb.String()
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

func (file_tree *FileTree) getFileIterator() <-chan string {
	ch := make(chan string, 1)
	go func() {
		var curr *FileTree
		var q []*FileTree
		q = append(q, file_tree)
		var pos int
		for len(q) > 0 {
			pos = len(q) - 1
			curr = q[pos]
			q = q[:pos]
			base := curr.getPath()
			for _, file := range curr.Files {
				ch <- base + file
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
	for _, c := range file_tree.Children {
		childrenList = append(childrenList, c)
	}
	return json.Marshal(&struct {
		*Alias
		Children []*FileTree `json:"children"`
	}{
		Alias:    (*Alias)(file_tree),
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
