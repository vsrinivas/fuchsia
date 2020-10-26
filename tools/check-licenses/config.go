// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"encoding/json"
	"errors"
	"os"
	"strings"
)

type CustomProjectLicense struct {
	ProjectRoot     string
	LicenseLocation string
}

// Config values are populated from the the json file at the default or user-specified path
type Config struct {
	FilesRegex                   []string               `json:"filesRegex,omitempty"`
	SkipDirs                     []string               `json:"skipDirs"`
	SkipFiles                    []string               `json:"skipFiles"`
	ProhibitedLicenseTypes       []string               `json:"prohibitedLicenseTypes"`
	TextExtensionList            []string               `json:"textExtensionList"`
	ExitOnProhibitedLicenseTypes bool                   `json:"exitOnProhibitedLicenseTypes"`
	ExitOnUnlicensedFiles        bool                   `json:"exitOnUnlicensedFiles"`
	OutputLicenseFile            bool                   `json:"outputLicenseFile"`
	MaxReadSize                  int64                  `json:"maxReadSize"`
	SeparatorWidth               int                    `json:"separatorWidth"`
	OutputFilePrefix             string                 `json:"outputFilePrefix"`
	OutputFileExtension          string                 `json:"outputFileExtension"`
	Product                      string                 `json:"product"`
	SingleLicenseFiles           []string               `json:"singleLicenseFiles"`
	LicensePatternDir            string                 `json:"licensePatternDir"`
	CustomProjectLicenses        []CustomProjectLicense `json:"customProjectLicenses"`
	BaseDir                      string                 `json:"baseDir"`
	Target                       string                 `json:"target"`
	LogLevel                     string                 `json:"logLevel"`
	TextExtensions               map[string]struct{}
}

// Init populates Config object with values found in the json config file.
//
// Both SkipFiles and SingleLicenseFiles are lowered.
func (c *Config) Init(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	d := json.NewDecoder(f)
	d.DisallowUnknownFields()
	if err = d.Decode(c); err != nil {
		return err
	}
	c.TextExtensions = map[string]struct{}{}
	for _, item := range c.TextExtensionList {
		c.TextExtensions[item] = struct{}{}
	}
	for i := range c.SingleLicenseFiles {
		c.SingleLicenseFiles[i] = strings.ToLower(c.SingleLicenseFiles[i])
	}
	for i := range c.SkipFiles {
		c.SkipFiles[i] = strings.ToLower(c.SkipFiles[i])
	}
	if c.Target != "all" {
		return errors.New("target must be \"all\"")
	}
	return nil
}
