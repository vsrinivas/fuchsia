// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

var Config *FileConfig

type FileConfig struct {
	// Path to the root of the fuchsia repository.
	FuchsiaDir string `json:"fuchsiaDir"`

	// Number of bytes to read in to capture copyright information
	// in regular source files.
	CopyrightSize int

	// Some characters in LICENSE texts are being parsed incorrectly.
	// Replace them with their utf8 equivalents so the resulting
	// NOTICE file renders it properly.
	Replacements []*Replacement

	// Extensions map is the list of filetypes that we can expect
	// may have license information included in it.
	Extensions map[string]bool
}

type Replacement struct {
	Replace string   `json:"replace"`
	With    string   `json:"with"`
	Notes   []string `json:"notes"`
}

func NewConfig() *FileConfig {
	return &FileConfig{
		CopyrightSize: 0,
		Replacements:  make([]*Replacement, 0),
		Extensions:    make(map[string]bool, 0),
	}
}

func (c *FileConfig) Merge(other *FileConfig) {
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	if c.CopyrightSize == 0 {
		c.CopyrightSize = other.CopyrightSize
	}
	c.Replacements = append(c.Replacements, other.Replacements...)
	for k, v := range other.Extensions {
		c.Extensions[k] = v
	}
}
