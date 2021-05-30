// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"encoding/json"
	"io/ioutil"
	"strings"
)

type CustomProjectLicense struct {
	Name            string
	ProjectRoot     string
	LicenseLocation string
}

// Config values are populated from the the json file at the default or user-specified path
type Config struct {
	DontSkipDirs []string `json:"dontSkipDirs"`
	SkipDirs     []string `json:"skipDirs"`
	SkipFiles    []string `json:"skipFiles"`

	TextExtensionList       []string `json:"textExtensionList"`
	StrictTextExtensionList []string `json:"strictTextExtensionList"`
	StrictAnalysis          bool     `json:"strictAnalysis"`
	SingleLicenseFiles      []string `json:"singleLicenseFiles"`
	NoticeFiles             []string `json:"noticeFiles"`
	StopLicensePropagation  []string `json:"stopLicensePropagation"`

	ProhibitedLicenseTypes       []string `json:"prohibitedLicenseTypes"`
	ExitOnDirRestrictedLicense   bool     `json:"exitOnDirRestrictedLicense"`
	ExitOnProhibitedLicenseTypes bool     `json:"exitOnProhibitedLicenseTypes"`
	ExitOnUnlicensedFiles        bool     `json:"exitOnUnlicensedFiles"`

	LicensePatternDir     string                 `json:"licensePatternDir"`
	CustomProjectLicenses []CustomProjectLicense `json:"customProjectLicenses"`
	FlutterLicenses       []string               `json:"flutterLicenses"`
	NoticeTxtFiles        []string               `json:"noticeTxtFiles"`
	LicenseAllowList      map[string][]string    `json:"licenseAllowList"`

	PrintFiles           bool     `json:"printFiles"`
	PrintProjects        bool     `json:"printProjects"`
	OutputLicenseFile    bool     `json:"outputLicenseFile"`
	OutputFilePrefix     string   `json:"outputFilePrefix"`
	OutputFileExtensions []string `json:"outputFileExtensions"`

	LogLevel    string `json:"logLevel"`
	MaxReadSize int    `json:"maxReadSize"`
	BaseDir     string `json:"baseDir"`
	OutDir      string `json:"outDir"`
	Target      string `json:"target"`
}

// NewConfig returns a config file representing the values found in the json file.
func NewConfig(path string) (*Config, error) {
	b, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}

	return NewConfigJson(string(b))
}

// NewConfigJson allows us to modify the content of the config before using it in tests.
func NewConfigJson(configJson string) (*Config, error) {
	c := &Config{}

	d := json.NewDecoder(strings.NewReader(configJson))
	d.DisallowUnknownFields()
	if err := d.Decode(c); err != nil {
		return nil, err
	}
	for i := range c.SingleLicenseFiles {
		c.SingleLicenseFiles[i] = strings.ToLower(c.SingleLicenseFiles[i])
	}
	for i := range c.SkipFiles {
		c.SkipFiles[i] = strings.ToLower(c.SkipFiles[i])
	}
	if c.BaseDir == "" {
		c.BaseDir = "."
	}
	if c.OutDir == "" {
		c.OutDir = "."
	}

	// Ensure we aren't skipping any directories in the CustomProjectLicenses map.
	for _, k := range c.CustomProjectLicenses {
		c.DontSkipDirs = append(c.DontSkipDirs, k.ProjectRoot)
	}

	return c, nil
}

// Merge two Config struct objects into one.
// - List fields are concatenated together.
// - boolean fields will be true if either one is true. (left || right)
// - Regular fields will be equal to the left struct field ("c") if it's not equal to the default value ("" for strings, 0 for ints),
//	otherwise they will be set to the right struct field.
func (c *Config) Merge(other *Config) {
	c.DontSkipDirs = append(c.DontSkipDirs, other.DontSkipDirs...)
	c.SkipDirs = append(c.SkipDirs, other.SkipDirs...)
	c.SkipFiles = append(c.SkipFiles, other.SkipFiles...)

	c.TextExtensionList = append(c.TextExtensionList, other.TextExtensionList...)
	c.StrictTextExtensionList = append(c.StrictTextExtensionList, other.StrictTextExtensionList...)
	c.StrictAnalysis = c.StrictAnalysis || other.StrictAnalysis
	c.SingleLicenseFiles = append(c.SingleLicenseFiles, other.SingleLicenseFiles...)
	c.StopLicensePropagation = append(c.StopLicensePropagation, other.StopLicensePropagation...)

	c.ProhibitedLicenseTypes = append(c.ProhibitedLicenseTypes, other.ProhibitedLicenseTypes...)
	c.ExitOnDirRestrictedLicense = c.ExitOnDirRestrictedLicense || other.ExitOnDirRestrictedLicense
	c.ExitOnProhibitedLicenseTypes = c.ExitOnProhibitedLicenseTypes || other.ExitOnProhibitedLicenseTypes
	c.ExitOnUnlicensedFiles = c.ExitOnUnlicensedFiles || other.ExitOnUnlicensedFiles

	if c.LicensePatternDir == "" {
		c.LicensePatternDir = other.LicensePatternDir
	}
	c.CustomProjectLicenses = append(c.CustomProjectLicenses, other.CustomProjectLicenses...)
	c.FlutterLicenses = append(c.FlutterLicenses, other.FlutterLicenses...)
	c.NoticeTxtFiles = append(c.NoticeTxtFiles, other.NoticeTxtFiles...)
	c.NoticeFiles = append(c.NoticeFiles, other.NoticeFiles...)
	if c.BaseDir == "" {
		c.BaseDir = other.BaseDir
	}
	if c.OutDir == "" {
		c.OutDir = other.OutDir
	}
	if c.Target == "" {
		c.Target = other.Target
	}
	if c.LogLevel == "" {
		c.LogLevel = other.LogLevel
	}

	if c.LicenseAllowList == nil {
		c.LicenseAllowList = make(map[string][]string)
	}
	if other.LicenseAllowList != nil {
		for k := range other.LicenseAllowList {
			c.LicenseAllowList[k] = append(c.LicenseAllowList[k], other.LicenseAllowList[k]...)
		}
	}

	c.PrintFiles = c.PrintFiles || other.PrintFiles
	c.PrintProjects = c.PrintProjects || other.PrintProjects
	c.OutputLicenseFile = c.OutputLicenseFile || other.OutputLicenseFile
	if c.OutputFilePrefix == "" {
		c.OutputFilePrefix = other.OutputFilePrefix
	}
	c.OutputFileExtensions = append(c.OutputFileExtensions, other.OutputFileExtensions...)

	if c.MaxReadSize == 0 {
		c.MaxReadSize = other.MaxReadSize
	}
	if c.BaseDir == "" {
		c.BaseDir = other.BaseDir
	}
	if c.OutDir == "" {
		c.OutDir = other.OutDir
	}
	if c.Target == "" {
		c.Target = other.Target
	}
}
