// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

type ResultConfig struct {
	FuchsiaDir        string      `json:"fuchsiaDir"`
	OutDir            string      `json:"outDir"`
	LicenseOutDir     string      `json:"licenseOutDir"`
	Outputs           []string    `json:"outputs"`
	Templates         []*Template `json:"templates"`
	Zip               bool        `json:"zip"`
	ExitOnError       bool        `json:"exitOnError"`
	GnGenOutputFile   string      `json:"gnGenOutputFile"`
	OutputLicenseFile bool        `json:"outputLicenseFile"`
	BuildInfoVersion  string      `json:"buildInfoVersion"`
	BuildInfoProduct  string      `json:"buildInfoProduct"`
	BuildInfoBoard    string      `json:"buildInfoBoard"`

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
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	if c.OutDir == "" {
		c.OutDir = other.OutDir
	}
	if c.LicenseOutDir == "" {
		c.LicenseOutDir = other.LicenseOutDir
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
	if c.BuildInfoVersion == "" {
		c.BuildInfoVersion = other.BuildInfoVersion
	}
	if c.BuildInfoProduct == "" {
		c.BuildInfoProduct = other.BuildInfoProduct
	}
	if c.BuildInfoBoard == "" {
		c.BuildInfoBoard = other.BuildInfoBoard
	}
}
