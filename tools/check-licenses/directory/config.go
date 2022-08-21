// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package directory

import (
	"path/filepath"
)

var Config *DirectoryConfig

type DirectoryConfig struct {
	FuchsiaDir string `json:"fuchsiaDir"`

	Skips                []*Skip `json:"skips"`
	ExitOnMissingProject bool    `json:"exitOnMissingProject"`
}

type Skip struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`

	// By default, "Paths" entries are full paths relative to $FUCHSIA_DIR.
	// However, paths like ".git" should be skipped everywhere in the fuchsia tree.
	//
	// Set this variable to tell check-licenses that the given paths are *not*
	// relative to $FUCHSIA_DIR, and should be skipped everywhere.
	SkipAnywhere bool `json:"skipAnywhere"`
}

func NewConfig() *DirectoryConfig {
	return &DirectoryConfig{
		Skips: make([]*Skip, 0),
	}
}

func (c *DirectoryConfig) shouldSkip(item string) bool {
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

func (c *DirectoryConfig) Merge(other *DirectoryConfig) {
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	c.Skips = append(c.Skips, other.Skips...)
	c.ExitOnMissingProject = c.ExitOnMissingProject || other.ExitOnMissingProject
}
