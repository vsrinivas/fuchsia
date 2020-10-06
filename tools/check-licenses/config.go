// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"encoding/json"
	"io/ioutil"
	"os"
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
	MaxReadSize                  int64                  `json:"maxReadSize"`
	SeparatorWidth               int                    `json:"separatorWidth"`
	OutputFilePrefix             string                 `json:"outputFilePrefix"`
	OutputFileExtension          string                 `json:"outputFileExtension"`
	Product                      string                 `json:"product"`
	SingleLicenseFiles           []string               `json:"singleLicenseFiles"`
	LicensePatternDir            string                 `json:"licensePatternDir"`
	CustomProjectLicenses        []CustomProjectLicense `json:"CustomProjectLicenses"`
	BaseDir                      string                 `json:"baseDir"`
	Target                       string                 `json:"target"`
	LogLevel                     string                 `json:"logLevel"`
	TextExtensions               map[string]struct{}
}

// Init populates Config object with values found in the json config file
func (config *Config) Init(configJson *string) error {
	jsonFile, err := os.Open(*configJson)
	defer jsonFile.Close()
	if err != nil {
		return err
	}
	byteValue, err := ioutil.ReadAll(jsonFile)
	if err != nil {
		return err
	}
	if err = json.Unmarshal(byteValue, &config); err != nil {
		return err
	}
	config.createTextExtensions()
	return nil
}

func (config *Config) createTextExtensions() {
	config.TextExtensions = make(map[string]struct{})
	for _, item := range config.TextExtensionList {
		config.TextExtensions[item] = struct{}{}
	}
}
