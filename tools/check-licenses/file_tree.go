// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"log"
	"os"
	"path/filepath"
	"runtime/trace"
	"sort"
	"strings"
	"sync"
)

// FileTree is an in memory representation of the state of the repository.
type FileTree struct {
	Name               string                `json:"name"`
	Path               string                `json:"path"`
	SingleLicenseFiles map[string][]*License `json:"project licenses"`
	LicenseMatches     map[string][]*Match   `json:"project license matches"`
	Files              []*File               `json:"files"`
	Children           []*FileTree           `json:"children"`
	Parent             *FileTree             `json:"-"`
	StrictAnalysis     bool                  `json:"strict analysis"`

	sync.RWMutex
}

// filetreeByPath implements sort.Interface for []*FileTree based on the Path field.
type filetreeByPath []*FileTree

func (a filetreeByPath) Len() int           { return len(a) }
func (a filetreeByPath) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a filetreeByPath) Less(i, j int) bool { return a[i].Path < a[j].Path }

// NewFileTree returns an instance of FileTree, given the input configuration
// file.
func NewFileTree(ctx context.Context, root string, parent *FileTree, config *Config, metrics *Metrics) (*FileTree, error) {
	defer trace.StartRegion(ctx, "NewFileTree").End()
	ft := FileTree{
		SingleLicenseFiles: make(map[string][]*License),
		LicenseMatches:     make(map[string][]*Match),
	}

	abs, err := filepath.Abs(root)
	if err != nil {
		return nil, err
	}

	ft.Name = filepath.Base(abs)
	ft.Path = abs
	ft.Parent = parent

	// If config.StrictAnalysis is true, we ignore all LICENSE files in the fuchsia directory.
	if parent == nil {
		ft.StrictAnalysis = config.StrictAnalysis
	} else {
		ft.StrictAnalysis = parent.StrictAnalysis
	}

	// If we are at a boundary where licenses change (e.g. "third_party" or "prebuilt" dirs),
	// turn off strict analysis. We don't have enough control over 3p repositories to enforce
	// having strict LICENSE information in all source files.
	for _, dirName := range config.StopLicensePropagation {
		if ft.Name == dirName {
			ft.StrictAnalysis = false
			break
		}
	}

	// Some projects include a license file, but they aren't at the topmost folder of the project.
	// Ideally, every project will also include a README.fuchsia file that points to the location
	// of the license file. Until we can work on that, we maintain a list of mappings in the
	// config.json file, mapping projects to their respective LICENSE files.
	for _, customProjectLicense := range config.CustomProjectLicenses {
		if strings.HasSuffix(root, customProjectLicense.ProjectRoot) {
			metrics.increment("num_single_license_files")
			licLocation := filepath.Join(root, customProjectLicense.LicenseLocation)
			ft.SingleLicenseFiles[licLocation] = []*License{}
			break
		}
	}

	entries, err := os.ReadDir(root)
	if err != nil {
		return nil, err
	}

	for _, entry := range entries {
		path := filepath.Join(root, entry.Name())

		skippable, err := isSkippable(config, path, entry)
		if err != nil {
			if strings.Contains(err.Error(), "no such file or directory") {
				log.Printf("skipping non-existing file: %s", path)
				continue
			}
			return nil, err
		}
		if skippable {
			log.Printf("skipping: %s", path)
			continue
		}

		if entry.IsDir() {
			child, err := NewFileTree(ctx, path, &ft, config, metrics)
			if err != nil {
				return nil, err
			}
			ft.Children = append(ft.Children, child)
			continue
		}

		newFile, err := NewFile(path, &ft)
		if err != nil {
			log.Printf("Warning: error creating file %v: %v\n", path, err)
			continue
		}

		// StrictAnalysis means don't rely on a project-level license. Verify that all source files
		// include license information in their headers.
		// TODO(jcecil): a file named LICENSE in the fuchsia tree will be
		// entirely skipped when running in strict analysis mode, since it
		// doesn't have a valid text extension. We should still analyze
		// these files, even if we don't add them as SingleLicenseFiles.
		if hasLowerPrefix(entry.Name(), config.SingleLicenseFiles) && !ft.StrictAnalysis {
			// Add project-level license files (e.g. LICENSE.txt, NOTICE.txt) to the SingleLicenseFiles map.
			metrics.increment("num_single_license_files")
			ft.SingleLicenseFiles[path] = []*License{}
			continue
		}

		if newFile.shouldProcess(ft.StrictAnalysis, config) {
			metrics.increment("num_non_single_license_files")
			ft.Files = append(ft.Files, newFile)
		} else {
			log.Printf("ignoring: %s", path)
			metrics.increment("num_extensions_excluded")
		}
	}

	sort.Sort(fileByPath(ft.Files))
	sort.Sort(filetreeByPath(ft.Children))

	return &ft, nil
}

// License information from parent directories should be copied to each of its children, recursively,
// until we hit a boundary that signifies we're in a directory that is no longer a part of the parent
// project (e.g. hitting a "third_party" directory).
func (ft *FileTree) propagateProjectLicenses(config *Config) {
	propagate := true
	for _, dirName := range config.StopLicensePropagation {
		if ft.Name == dirName {
			propagate = false
			break
		}
	}

	if propagate && ft.Parent != nil {
		for key, val := range ft.Parent.SingleLicenseFiles {
			ft.SingleLicenseFiles[key] = val
		}
	}

	for _, child := range ft.Children {
		child.propagateProjectLicenses(config)
	}
}

func (ft *FileTree) getFileTreeIterator() <-chan *FileTree {
	ch := make(chan *FileTree, 1)
	go func() {
		var curr *FileTree
		var q []*FileTree
		q = append(q, ft)
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

func (ft *FileTree) getFileIterator() <-chan *File {
	ch := make(chan *File, 1)
	go func() {
		var curr *FileTree
		var q []*FileTree
		q = append(q, ft)
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

func isSkippable(config *Config, path string, entry os.DirEntry) (bool, error) {
	for _, skipFile := range config.SkipFiles {
		if strings.ToLower(entry.Name()) == skipFile || strings.ToLower(path) == skipFile {
			return true, nil
		}
	}

	if entry.IsDir() && len(entry.Name()) > 1 && (strings.HasPrefix(entry.Name(), ".") || strings.HasPrefix(entry.Name(), "__")) {
		return true, nil
	}

	info, err := entry.Info()
	if err != nil {
		return true, err
	}
	if info.Size() == 0 {
		// An empty file has no content to copyright. Skip.
		return true, nil
	}

	sep := string(filepath.Separator)
	skippable := false
	for _, skipDir := range config.SkipDirs {
		if skipDir == path || strings.HasPrefix(path, skipDir+sep) {
			skippable = true
			break
		}
	}
	if !skippable {
		return false, nil
	}

	for _, keepDir := range config.DontSkipDirs {
		if keepDir == path ||
			strings.HasPrefix(path, keepDir+sep) ||
			strings.HasPrefix(keepDir, path+sep) {
			return false, nil
		}
	}
	return true, nil
}
