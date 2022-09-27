// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"path/filepath"
)

var Config *ProjectConfig

func init() {
	Config = &ProjectConfig{}
}

type ProjectConfig struct {
	FuchsiaDir string `json:"fuchsiaDir"`

	OutputLicenseFile bool `json:"outputLicenseFile"`

	// Paths to temporary directories holding README.fuchsia files.
	// These files will eventually migrate to their correct locations in
	// the Fuchsia repository.
	Readmes []*Readme `json:"readmes"`

	// Keywords signifying where the license information for one project
	// ends, and the license info for another project begins.
	// (e.g. "third_party")
	Barriers []*Barrier `json:"barriers"`
}

type Readme struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

type Barrier struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

// IsBarrier returns true if the given path is a part of the parent project.
// For example, directories under //third_party are independent projects that
// the parent Fuchsia license file may not apply to.
//
// These "barrier" directories are set in the config file.
func IsBarrier(path string) bool {
	base := filepath.Base(path)
	for _, barrier := range Config.Barriers {
		for _, path := range barrier.Paths {
			if base == path {
				return true
			}
		}
	}
	return false
}

func NewConfig() *ProjectConfig {
	return &ProjectConfig{
		Readmes:  make([]*Readme, 0),
		Barriers: make([]*Barrier, 0),
	}
}

func (c *ProjectConfig) Merge(other *ProjectConfig) {
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	c.Readmes = append(c.Readmes, other.Readmes...)
	c.Barriers = append(c.Barriers, other.Barriers...)
	c.OutputLicenseFile = c.OutputLicenseFile || other.OutputLicenseFile
}
