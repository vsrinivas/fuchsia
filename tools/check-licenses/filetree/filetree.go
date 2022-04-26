// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetree

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

// FileTree is an in memory representation of the state of the repository.
type FileTree struct {
	Name      string             `json:"name,omitempty"`
	Path      string             `json:"path,omitempty"`
	FilePaths []string           `json:"files,omitempty"`
	Children  []*FileTree        `json:"children,omitempty"`
	Parent    *FileTree          `json:"-"`
	Projects  []*project.Project `json:"project,omitempty"`

	sync.RWMutex
}

// Order implements sort.Interface for []*FileTree based on the Path field.
type Order []*FileTree

func (a Order) Len() int           { return len(a) }
func (a Order) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a Order) Less(i, j int) bool { return a[i].Path < a[j].Path }

// NewFileTree returns an instance of FileTree.
func NewFileTree(root string, parent *FileTree) (*FileTree, error) {
	ft := FileTree{}
	if RootFileTree == nil {
		RootFileTree = &ft
	}

	ft.Name = filepath.Base(root)
	ft.Path = root
	ft.Parent = parent

	// If we are not at a "barrier" directory (e.g. prebuilt, third_party),
	// then the license info of the parent directory also applies to this directory.
	// Propagate that information down here.
	if !project.IsBarrier(root) && parent != nil {
		ft.Projects = append(ft.Projects, parent.Projects...)
	}

	directoryContents, err := os.ReadDir(root)
	if err != nil {
		return nil, err
	}

	// First, find the README.fuchsia file in the current directory (if it exists),
	// or retrieve the project instance from any predefined "readmes" directory.
	p, err := project.NewProject(filepath.Join(root, "README.fuchsia"), root)
	if err != nil {
		if os.IsNotExist(err) {
			// There is no README.fuchsia file in the current directory.
			// That's OK. Continue.
		} else {
			return nil, err
		}
	} else {
		ft.Projects = append(ft.Projects, p)
	}

	// Then traverse the rest of the contents of this directory.
	for _, item := range directoryContents {
		path := filepath.Join(root, item.Name())
		if Config.shouldSkip(path) {
			plusVal(Skipped, path)
			continue
		}

		// Directories
		if item.IsDir() {
			plus1(NumFolders)
			child, err := NewFileTree(path, &ft)
			if err != nil {
				return nil, err
			}
			ft.Children = append(ft.Children, child)
			continue
		}

		// Files
		fi, err := os.Stat(path)
		if err != nil {
			// Likely a symlink issue
			continue
		}
		if fi.Size() == 0 {
			// Ignore empty files
			continue
		} else {
			plus1(NumFiles)
			ft.FilePaths = append(ft.FilePaths, path)
		}
	}

	sort.Sort(Order(ft.Children))
	sort.Sort(project.Order(ft.Projects))

	// Verify that all files in the current directory belong to a project.
	if len(ft.Projects) == 0 {
		for _, f := range ft.FilePaths {
			plusVal(FileMissingProject, f)
		}
		if len(ft.FilePaths) > 0 {
			plusVal(FolderMissingProject, ft.Path)
			if Config.ExitOnMissingProject {
				return nil, fmt.Errorf("Filetree %v has no associated projects", root)
			}
		}
	}
	for _, p := range ft.Projects {
		p.AddFiles(ft.FilePaths)
	}

	AllFileTrees[ft.Path] = &ft
	return &ft, nil
}
