// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package project

import (
	"path/filepath"
	"regexp"
)

var Config *ProjectConfig

func init() {
	Config = &ProjectConfig{}
}

type ProjectConfig struct {
	FuchsiaDir string `json:"fuchsiaDir"`

	// Paths to temporary directories holding README.fuchsia files.
	// These files will eventually migrate to their correct locations in
	// the Fuchsia repository.
	Readmes []*Readme `json:"readmes"`

	// Keywords signifying where the license information for one project
	// ends, and the license info for another project begins.
	// (e.g. "third_party")
	Barriers []*Barrier `json:"barriers"`

	// Project regexs that should be included in the final NOTICE file.
	//
	// Everything is included by default. This field should only be used
	// if something was explicitly excluded, but needs to be brought back.
	Includes []*Include `json:"includes"`

	// Project regexs that should be excluded from the final NOTICE file.
	Excludes []*Exclude `json:"excludes"`

	ContinueOnError bool `json:"continueOnError"`
}

type Readme struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

type Barrier struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

type Include struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

type Exclude struct {
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
		Includes: make([]*Include, 0),
		Excludes: make([]*Exclude, 0),
	}
}

func (c *ProjectConfig) shouldInclude(p *Project) (bool, error) {
	include := true
	for _, e := range c.Excludes {
		for _, path := range e.Paths {
			m, err := regexp.MatchString(path, p.Root)
			if err != nil {
				return false, err
			}
			if m {
				include = false
				break
			}
		}
	}
	if !include {
		for _, i := range c.Includes {
			for _, path := range i.Paths {
				m, err := regexp.MatchString(path, p.Root)
				if err != nil {
					return false, err
				}
				if m {
					include = true
					break
				}
			}
		}
	}
	return include, nil
}
func (c *ProjectConfig) Merge(other *ProjectConfig) {
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	c.Readmes = append(c.Readmes, other.Readmes...)
	c.Barriers = append(c.Barriers, other.Barriers...)
	c.Includes = append(c.Includes, other.Includes...)
	c.Excludes = append(c.Excludes, other.Excludes...)
	c.ContinueOnError = c.ContinueOnError || other.ContinueOnError
}
