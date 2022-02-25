// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetree

var Config *FileTreeConfig

func init() {
	Config = &FileTreeConfig{}
}

type FileTreeConfig struct {
	Skips                []Skip `json:"skips"`
	ExitOnMissingProject bool   `json:"exitOnMissingProject"`
}

type Skip struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

func (c *FileTreeConfig) shouldSkip(item string) bool {
	for _, skip := range c.Skips {
		for _, path := range skip.Paths {
			if item == path {
				return true
			}
		}
	}
	return false
}

func (c *FileTreeConfig) Merge(other *FileTreeConfig) {
	c.Skips = append(c.Skips, other.Skips...)
	c.ExitOnMissingProject = c.ExitOnMissingProject || other.ExitOnMissingProject
}
