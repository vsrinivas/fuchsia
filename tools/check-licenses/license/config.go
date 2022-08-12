// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

type LicenseConfig struct {
	FuchsiaDir   string         `json:"fuchsiaDir"`
	PatternRoots []*PatternRoot `json:"patternRoot"`
	AllowLists   []*AllowList   `json:"allowlists"`
}

type PatternRoot struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

type AllowList struct {
	Projects []string `json:"paths"`
	Patterns []string `json:"patterns"`
	Notes    []string `json:"notes"`
}

var Config *LicenseConfig

func init() {
	Config = NewConfig()
}

func NewConfig() *LicenseConfig {
	return &LicenseConfig{
		PatternRoots: make([]*PatternRoot, 0),
		AllowLists:   make([]*AllowList, 0),
	}
}

func (c *LicenseConfig) Merge(other *LicenseConfig) {
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	if c.PatternRoots == nil {
		c.PatternRoots = make([]*PatternRoot, 0)
	}
	if other.PatternRoots == nil {
		other.PatternRoots = make([]*PatternRoot, 0)
	}
	c.PatternRoots = append(c.PatternRoots, other.PatternRoots...)

	if c.AllowLists == nil {
		c.AllowLists = make([]*AllowList, 0)
	}
	if other.AllowLists == nil {
		other.AllowLists = make([]*AllowList, 0)
	}
	c.AllowLists = append(c.AllowLists, other.AllowLists...)
}
