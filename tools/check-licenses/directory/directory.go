// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package directory

import (
	"os"
	"path/filepath"
	"sort"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
)

// Directory is an in memory representation of the state of the repository.
type Directory struct {
	Name      string           `json:"name,omitempty"`
	Path      string           `json:"path,omitempty"`
	FilePaths []string         `json:"files,omitempty"`
	Children  []*Directory     `json:"children,omitempty"`
	Parent    *Directory       `json:"-"`
	Project   *project.Project `json:"project,omitempty"`

	sync.RWMutex
}

// Order implements sort.Interface for []*Directory based on the Path field.
type Order []*Directory

func (a Order) Len() int           { return len(a) }
func (a Order) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a Order) Less(i, j int) bool { return a[i].Path < a[j].Path }

// NewDirectory returns an instance of Directory.
func NewDirectory(root string, parent *Directory) (*Directory, error) {
	d := Directory{}
	if RootDirectory == nil {
		RootDirectory = &d
	}

	d.Name = filepath.Base(root)
	d.Path = root
	d.Parent = parent

	// If we are not at a "barrier" directory (e.g. prebuilt, third_party),
	// then the license info of the parent directory also applies to this directory.
	// Propagate that information down here.
	if !project.IsBarrier(root) && parent != nil {
		d.Project = parent.Project
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
		d.Project = p
	}

	// Then traverse the rest of the contents of this directory.
	for _, item := range directoryContents {
		path := filepath.Join(root, item.Name())

		// Check the config file to see if we should skip this file / folder.
		if Config.shouldSkip(path) {
			plusVal(Skipped, path)
			continue
		}

		// Directories
		if item.IsDir() {
			plus1(NumFolders)
			child, err := NewDirectory(path, &d)
			if err != nil {
				return nil, err
			}
			d.Children = append(d.Children, child)
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
			d.FilePaths = append(d.FilePaths, path)
		}
	}

	sort.Sort(Order(d.Children))

	// Add all of the files we found to the current "Project".
	if d.Project != nil {
		d.Project.AddFiles(d.FilePaths)
	}

	AllDirectories[d.Path] = &d
	return &d, nil
}
