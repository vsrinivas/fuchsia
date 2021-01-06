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
	SkipDirs                     []string               `json:"skipDirs"`
	SkipFiles                    []string               `json:"skipFiles"`
	ProhibitedLicenseTypes       []string               `json:"prohibitedLicenseTypes"`
	TextExtensionList            []string               `json:"textExtensionList"`
	StrictTextExtensionList      []string               `json:"strictTextExtensionList"`
	ExitOnProhibitedLicenseTypes bool                   `json:"exitOnProhibitedLicenseTypes"`
	ExitOnUnlicensedFiles        bool                   `json:"exitOnUnlicensedFiles"`
	StrictAnalysis               bool                   `json:"strictAnalysis"`
	OutputLicenseFile            bool                   `json:"outputLicenseFile"`
	MaxReadSize                  int                    `json:"maxReadSize"`
	OutputFilePrefix             string                 `json:"outputFilePrefix"`
	OutputFileExtension          string                 `json:"outputFileExtension"`
	SingleLicenseFiles           []string               `json:"singleLicenseFiles"`
	StopLicensePropagation       []string               `json:"stopLicensePropagation"`
	LicensePatternDir            string                 `json:"licensePatternDir"`
	CustomProjectLicenses        []CustomProjectLicense `json:"customProjectLicenses"`
	FlutterLicenses              []string               `json:"flutterLicenses"`
	NoticeTxtFiles               []string               `json:"noticeTxtFiles"`
	BaseDir                      string                 `json:"baseDir"`
	Target                       string                 `json:"target"`
	LogLevel                     string                 `json:"logLevel"`
}

// Init populates Config object with values found in the json config file.
//
// Both SkipFiles and SingleLicenseFiles are lowered.
func NewConfig(path string) (*Config, error) {
	c := &Config{}

	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	d := json.NewDecoder(f)
	d.DisallowUnknownFields()
	if err = d.Decode(c); err != nil {
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
	if c.Target != "all" {
		return nil, errors.New("target must be \"all\"")
	}
	return c, nil
}
