// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

var Config *FileConfig

type FileConfig struct {
	// Number of bytes to read in to capture copyright information
	// in regular source files.
	CopyrightSize int

	// Some characters in LICENSE texts are being parsed incorrectly.
	// Replace them with their utf8 equivalents so the resulting
	// NOTICE file renders it properly.
	Replacements []*Replacement
}

type Replacement struct {
	Replace string   `json:"replace"`
	With    string   `json:"with"`
	Notes   []string `json:"notes"`
}

func NewFileConfig() *FileConfig {
	return &FileConfig{
		CopyrightSize: 0,
		Replacements:  make([]*Replacement, 0),
	}
}

func (c *FileConfig) Merge(other *FileConfig) {
	if c.CopyrightSize == 0 {
		c.CopyrightSize = other.CopyrightSize
	}
	c.Replacements = append(c.Replacements, other.Replacements...)
}
