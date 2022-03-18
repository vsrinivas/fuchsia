// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

type ResultConfig struct {
	FuchsiaDir string      `json:"fuchsiaDir"`
	OutputDir  string      `json:"outputdir"`
	Outputs    []string    `json:"outputs"`
	Templates  []*Template `json:"templates"`
	Zip        bool        `json:"zip"`

	DiffNotices []string `json:"diffnotices"`
}

type Template struct {
	Paths []string `json:"paths"`
	Notes []string `json:"notes"`
}

type DiffNotice struct {
	Type string `json:"type"`
	Path string `json:"path"`
}

var Config *ResultConfig

func NewResultConfig() *ResultConfig {
	return &ResultConfig{
		Outputs:   make([]string, 0),
		Templates: make([]*Template, 0),
	}
}

func (c *ResultConfig) Merge(other *ResultConfig) {
	if c.OutputDir == "" {
		c.OutputDir = other.OutputDir
	}
	c.Templates = append(c.Templates, other.Templates...)
	c.Outputs = append(c.Outputs, other.Outputs...)
	c.Zip = c.Zip || other.Zip
	c.DiffNotices = append(c.DiffNotices, other.DiffNotices...)
}
