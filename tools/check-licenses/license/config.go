// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

type LicenseConfig struct {
	FuchsiaDir   string         `json:"fuchsiaDir"`
	PatternRoots []*PatternRoot `json:"patternRoot"`
	AllowLists   []*AllowList   `json:"allowlists"`
	Exceptions   []*Exception   `json:"exceptions"`
}

type PatternRoot struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

// Projects must only contain approved license types.
//
// If they have any license texts that are not approved, an exception must
// exist to allow that project access to that license type.
//
// This struct describes how the allowlist is formatted.
// It is named "Exception" instead of "Allowlist" since the old format for
// allowlists is currently being used. (soft transition)
// TODO(fxbug.dev/109828): Rename "Exception" to "Allowlist".
type Exception struct {
	Notes       []string          `json:"notes"`
	LicenseType string            `json:"licenseType"`
	Example     string            `json:"example"`
	PatternRoot string            `json:"patternRoot"`
	Entries     []*ExceptionEntry `json:"entries"`
}

// Each exception can define a bug which should describe why / when the project
// was allowlisted.
//
// In the future, bug entries will be required for each exception entry.
type ExceptionEntry struct {
	Bug      string   `json:"bug"`
	Projects []string `json:"projects"`
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
	c.Exceptions = append(c.Exceptions, other.Exceptions...)
}
