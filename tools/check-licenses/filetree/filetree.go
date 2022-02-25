// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetree

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"runtime/trace"
	"sort"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

// FileTree is an in memory representation of the state of the repository.
type FileTree struct {
	Name     string             `json:"name,omitempty"`
	Path     string             `json:"path,omitempty"`
	Files    []string           `json:"files,omitempty"`
	Children []*FileTree        `json:"children,omitempty"`
	Parent   *FileTree          `json:"-"`
	Projects []*project.Project `json:"project,omitempty"`

	sync.RWMutex
}

// filetreeByPath implements sort.Interface for []*FileTree based on the Path field.
type filetreeByPath []*FileTree

func (a filetreeByPath) Len() int           { return len(a) }
func (a filetreeByPath) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a filetreeByPath) Less(i, j int) bool { return a[i].Path < a[j].Path }

// NewFileTree returns an instance of FileTree.
func NewFileTree(ctx context.Context, root string, parent *FileTree) (*FileTree, error) {
	defer trace.StartRegion(ctx, "NewFileTree").End()
	ft := FileTree{}

	ft.Name = filepath.Base(root)
	ft.Path = root
	ft.Parent = parent

	// If we are not at a "barrier" directory (e.g. prebuilt, thirdparty),
	// then the license info of the parent directory also applies to this directory.
	// Propagate that information down here.
	if !project.IsBarrier(root) && parent != nil {
		ft.Projects = append(ft.Projects, parent.Projects...)
	}

	directoryContents, err := os.ReadDir(root)
	if err != nil {
		return nil, err
	}

	// First, find all README.fuchsia files in the current directory,
	// and create Project structs for them.
	for _, item := range directoryContents {
		path := filepath.Join(root, item.Name())
		if item.Name() == "README.fuchsia" {
			p, err := project.NewProject(path)
			if err != nil {
				return nil, err
			}
			ft.Projects = append(ft.Projects, p)
		}
	}

	// Then traverse the rest of the contents of this directory.
	for _, item := range directoryContents {
		path := filepath.Join(root, item.Name())
		if Config.shouldSkip(path) {
			continue
		}

		// Directories
		if item.IsDir() {
			plus1(NumFolders)
			child, err := NewFileTree(ctx, path, &ft)
			if err != nil {
				return nil, err
			}
			ft.Children = append(ft.Children, child)
			continue
		}

		// Files
		plus1(NumFiles)
		ft.Files = append(ft.Files, path)
	}

	sort.Sort(filetreeByPath(ft.Children))

	// Verify that all files in the current directory belong to a project.
	if len(ft.Projects) == 0 && len(ft.Files) > 0 {
		plusVal(MissingProject, ft.Path)
		if Config.ExitOnMissingProject {
			return nil, fmt.Errorf("Filetree %v has no associated projects", root)
		}
	}
	for _, p := range ft.Projects {
		p.AddFiles(ft.Files)
	}

	return &ft, nil
}
