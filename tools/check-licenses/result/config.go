// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

type ResultConfig struct {
	DepFile string `json:"depFile"`
	CWD     string `json:"cwd"`

	FuchsiaDir        string      `json:"fuchsiaDir"`
	OutDir            string      `json:"outDir"`
	BuildDir          string      `json:"buildDir"`
	Outputs           []string    `json:"outputs"`
	Templates         []*Template `json:"templates"`
	Zip               bool        `json:"zip"`
	ExitOnError       bool        `json:"exitOnError"`
	GnGenOutputFile   string      `json:"gnGenOutputFile"`
	OutputLicenseFile bool        `json:"outputLicenseFile"`

	DiffNotice string `json:"diffnotice"`
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

func NewConfig() *ResultConfig {
	return &ResultConfig{
		Outputs:           make([]string, 0),
		Templates:         make([]*Template, 0),
		OutputLicenseFile: true,
	}
}

func (c *ResultConfig) Merge(other *ResultConfig) {
	if c.CWD == "" {
		c.CWD = other.CWD
	}
	if c.BuildDir == "" {
		c.BuildDir = other.BuildDir
	}
	if c.DepFile == "" {
		c.DepFile = other.DepFile
	}
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	if c.OutDir == "" {
		c.OutDir = other.OutDir
	}
	c.Templates = append(c.Templates, other.Templates...)
	c.Outputs = append(c.Outputs, other.Outputs...)
	c.Zip = c.Zip || other.Zip
	if c.DiffNotice == "" {
		c.DiffNotice = other.DiffNotice
	}
	c.ExitOnError = c.ExitOnError || other.ExitOnError
	if c.GnGenOutputFile == "" {
		c.GnGenOutputFile = other.GnGenOutputFile
	}
	c.OutputLicenseFile = c.OutputLicenseFile || other.OutputLicenseFile
}
