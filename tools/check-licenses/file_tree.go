// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"fmt"
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
	name               string
	children           map[string]*FileTree
	files              []string
	singleLicenseFiles map[string][]*License
	parent             *FileTree

	sync.RWMutex
}

func (license_file_tree *FileTree) Init() {
	license_file_tree.children = make(map[string]*FileTree)
	license_file_tree.singleLicenseFiles = make(map[string][]*License)
}

func (file_tree *FileTree) getSetCurr(path string) *FileTree {
	children := strings.Split(filepath.Dir(path), "/")
	curr := file_tree
	file_tree.Lock()
	for _, child := range children {
		if _, found := curr.children[child]; !found {
			curr.children[child] = &FileTree{name: child, parent: curr}
			curr.children[child].Init()
		}
		curr = curr.children[child]
	}
	file_tree.Unlock()
	return curr
}

func (file_tree *FileTree) addFile(path string) {
	curr := file_tree.getSetCurr(path)
	curr.files = append(curr.files, filepath.Base(path))
}

func (file_tree *FileTree) addSingleLicenseFile(path string, base string) {
	curr := file_tree.getSetCurr(path)
	curr.singleLicenseFiles[base] = []*License{}
}

func (file_tree *FileTree) getProjectLicense(path string) *FileTree {
	curr := file_tree
	var gold *FileTree
	pieces := strings.Split(filepath.Dir(path), "/")
	for _, piece := range pieces {
		if len(curr.singleLicenseFiles) > 0 {
			gold = curr
		}
		curr.RLock()
		if _, found := curr.children[piece]; !found {
			curr.RUnlock()
			break
		}
		currNext := curr.children[piece]
		curr.RUnlock()
		curr = currNext
	}
	if len(pieces) > 1 && len(curr.singleLicenseFiles) > 0 {
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
		arr = append(arr, curr.name)
		curr = curr.parent
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
			if len(curr.singleLicenseFiles) > 0 {
				ch <- curr
			}
			curr.RLock()
			for _, child := range curr.children {
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
			for _, file := range curr.files {
				ch <- base + file
			}
			curr.RLock()
			for _, child := range curr.children {
				q = append(q, child)
			}
			curr.RUnlock()
		}
		close(ch)
	}()
	return ch
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
