// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetree

import (
	"path/filepath"
)

var Config *FileTreeConfig

type FileTreeConfig struct {
	FuchsiaDir           string  `json:"fuchsiaDir"`
	Skips                []*Skip `json:"skips"`
	ExitOnMissingProject bool    `json:"exitOnMissingProject"`
}

type Skip struct {
	FuchsiaDir string `json:"fuchsiaDir"`

	Paths []string `json:"paths"`
	Notes []string `json:"notes"`

	// By default, "Paths" entries are full paths relative to $FUCHSIA_DIR.
	// However, paths like ".git" should be skipped everywhere in the fuchsia tree.
	//
	// Set this variable to tell check-licenses that the given paths are *not*
	// relative to $FUCHSIA_DIR, and should be skipped everywhere.
	SkipAnywhere bool `json:"skipAnywhere"`
}

func NewFileTreeConfig() *FileTreeConfig {
	return &FileTreeConfig{
		Skips: make([]*Skip, 0),
	}
}

func (c *FileTreeConfig) shouldSkip(item string) bool {
	base := filepath.Base(item)
	for _, skip := range c.Skips {
		for _, path := range skip.Paths {
			if item == path {
				return true
			} else if skip.SkipAnywhere && base == path {
				return true
			}
		}
	}
	return false
}

func (c *FileTreeConfig) Merge(other *FileTreeConfig) {
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	c.Skips = append(c.Skips, other.Skips...)
	c.ExitOnMissingProject = c.ExitOnMissingProject || other.ExitOnMissingProject
}
