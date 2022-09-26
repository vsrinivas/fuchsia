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

	// URL overrides can be defined in the config file.
	FileDataURLs []*FileDataURL `json:"urlReplacements"`
}

// Prebuilt libraries have licenses that come from various locations.
// We don't have access to the source URLs for those dependent libraries.
// FileDataURL lets us maintain a separate mapping of library name -> URL,
// which we can use in check-licenses to produce the compliance worksheet.
type FileDataURL struct {
	Source       string            `json:"source"`
	Prefix       string            `json:"prefix"`
	Projects     map[string]bool   `json:"projects"`
	Products     map[string]bool   `json:"products"`
	Boards       map[string]bool   `json:"boards"`
	Replacements map[string]string `json:"replacements"`
}

// Support replacing individual characters with other ones.
// For example, sometimes golang processes ` incorrectly, so we can replace
// instances of that character with ' using Replacement fields in the
// config file.
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
		FileDataURLs:  make([]*FileDataURL, 0),
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
	c.FileDataURLs = append(c.FileDataURLs, other.FileDataURLs...)
}
