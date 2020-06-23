// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// NewFileTree returns an instance of FileTree, given the input configuration file.
func NewFileTree(config *Config, metrics *Metrics) *FileTree {
	var file_tree FileTree
	file_tree.Init()
	var root string
	if config.Target == "all" {
		root = config.BaseDir
		err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				fmt.Printf("error walking the path %q: %v\n", root, err)
				return err
			}
			if info.IsDir() {
				// TODO regex instead of exact match
				for _, skipDir := range config.SkipDirs {
					if info.Name() == skipDir {
						fmt.Printf("skipping a dir without errors: %+v \n", info.Name())
						return filepath.SkipDir
					}
				}
				return nil
			} else {
				for _, skipFile := range config.SkipFiles {
					if info.Name() == skipFile {
						fmt.Printf("skipping a file without errors: %+v \n", info.Name())
						return nil
					}
				}
			}
			if isSingleLicenseFile(info.Name(), config.SingleLicenseFiles) {
				metrics.num_single_license_files++
				file_tree.addSingleLicenseFile(path)
			} else {
				if isValidExtension(path, config) {
					metrics.num_non_single_license_files++
					file_tree.addFile(path)
				} else {
					metrics.num_extensions_excluded++
				}
			}
			return nil
		})
		if err != nil {
			fmt.Println(err.Error())
		}
	} else {
		// TODO target specific file tree
	}

	return &file_tree
}

// FileTree is an in memory representation of the state of the repository.
type FileTree struct {
	name               string
	children           map[string]*FileTree
	files              []string
	singleLicenseFiles []string
	parent             *FileTree
}

func (license_file_tree *FileTree) Init() {
	license_file_tree.children = make(map[string]*FileTree)
}

func (file_tree *FileTree) getSetCurr(path string) *FileTree {
	children := strings.Split(filepath.Dir(path), "/")
	curr := file_tree
	for _, child := range children {
		if _, found := curr.children[child]; !found {
			curr.children[child] = &FileTree{name: child, parent: curr}
			curr.children[child].Init()
		}
		curr = curr.children[child]
	}
	return curr
}

func (file_tree *FileTree) addFile(path string) {
	curr := file_tree.getSetCurr(path)
	curr.files = append(curr.files, filepath.Base(path))
}

func (file_tree *FileTree) addSingleLicenseFile(path string) {
	curr := file_tree.getSetCurr(path)
	curr.singleLicenseFiles = append(curr.singleLicenseFiles, filepath.Base(path))
}

func (file_tree *FileTree) getProjectLicense(path string) *FileTree {
	curr := file_tree
	var gold *FileTree
	pieces := strings.Split(filepath.Dir(path), "/")
	for _, piece := range pieces {
		if len(curr.singleLicenseFiles) > 0 {
			gold = curr
		}
		if _, found := curr.children[piece]; !found {
			break
		}
		curr = curr.children[piece]
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

func isValidExtension(path string, config *Config) bool {
	extension := filepath.Ext(path)
	if len(extension) == 0 {
		return false
	}
	_, found := config.TextExtensions[extension[1:]]
	return found
}
