// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"fmt"
	"os"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/directory"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/result"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/result/world"
)

var (
	// Map of string to string.
	//
	// Config files may contain unexpanded variables like {FUCHSIA_DIR}.
	// This map is used to replace those variables with values set on the command-line.
	// Map keys are variable names. Map values are what they will be replaced with.
	ConfigVars map[string]string
)

func init() {
	ConfigVars = make(map[string]string)
}

type CheckLicensesConfig struct {
	// LogLevel controls how much output is printed to stdout.
	// See log.go for more information.
	LogLevel int `json:"logLevel"`
	// FuchsiaDir is the path to the root of your fuchsia workspace.
	// Typically ~/fuchsia, but can be set by environment variables
	// or command-line arguments.
	FuchsiaDir string `json:"fuchsiaDir"`
	// OutDir is the path to the output directory for your GN workspace.
	// Typically ~/fuchsia/out/default, but can be set by environment variables
	// or command-line arguments.
	OutDir string `json:"outDir"`

	// Includes defines a list of files or directories that contain
	// config.json files. This allows check-licenses configuration details
	// to be spread out across the fuchsia workspace.
	Includes []Include `json:"includes"`

	// The following variables represent Config files for the
	// check-licenses subpackage of the same name.
	File      *file.FileConfig           `json:"file"`
	License   *license.LicenseConfig     `json:"license"`
	Project   *project.ProjectConfig     `json:"project"`
	Directory *directory.DirectoryConfig `json:"directory"`
	Result    *result.ResultConfig       `json:"result"`
	World     *world.WorldConfig         `json:"world"`

	// On the command-line, a user can provide a GN target (e.g. //sdk)
	// to generate a NOTICE file for.
	Target string `json:"target"`
	// The SDK version for the current workspace.
	BuildInfoVersion string `json:"buildInfoVersion"`
	// The product currently set in the fx args for the local workspace.
	BuildInfoProduct string `json:"buildInfoProduct"`
	// The board currently set in the fx args for the local workspace.
	BuildInfoBoard string `json:"buildInfoBoard"`

	// Flag stating whether or not check-licenses should generate
	// a NOTICE file.
	OutputLicenseFile bool `json:"outputLicenseFile"`

	Filters []string
}

// Create a new CheckLicensesConfig object by reading in a config.json file.
func NewCheckLicensesConfig(path string) (*CheckLicensesConfig, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("Failed to read config file [%v]: %w\n", path, err)
	}

	c, err := NewCheckLicensesConfigJson(string(b))
	if err != nil {
		return nil, fmt.Errorf("Failed to parse JSON config file [%v]: %v\n", path, err)
	}
	return c, nil
}

// Create a new CheckLicensesConfig object by consuming a json config string.
func NewCheckLicensesConfigJson(configJson string) (*CheckLicensesConfig, error) {
	for k, v := range ConfigVars {
		configJson = strings.ReplaceAll(configJson, k, v)
	}

	// Make sure all variables have been replaced.
	r := regexp.MustCompile(`({[\w]+})`)
	matches := r.FindAllStringSubmatch(configJson, -1)

	if len(matches) > 0 {
		return nil, fmt.Errorf("Found unexpanded variable(s) in config file: %v\n", configJson)
	}

	c := &CheckLicensesConfig{
		File:      file.NewConfig(),
		License:   license.NewConfig(),
		Project:   project.NewConfig(),
		Directory: directory.NewConfig(),
		Result:    result.NewConfig(),
		World:     world.NewConfig(),
	}

	d := json.NewDecoder(strings.NewReader(configJson))
	d.DisallowUnknownFields()
	if err := d.Decode(c); err != nil {
		return nil, err
	}

	if err := c.ProcessIncludes(); err != nil {
		return nil, err
	}

	return c, nil
}

// Merge two CheckLicenseConfig objects together.
func (c *CheckLicensesConfig) Merge(other *CheckLicensesConfig) error {
	c.File.Merge(other.File)
	c.License.Merge(other.License)
	c.Directory.Merge(other.Directory)
	c.Project.Merge(other.Project)
	c.Result.Merge(other.Result)
	c.World.Merge(other.World)

	c.Includes = append(c.Includes, other.Includes...)

	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	if c.OutDir == "" {
		c.OutDir = other.OutDir
	}

	if other.LogLevel > c.LogLevel {
		c.LogLevel = other.LogLevel
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

	c.Filters = append(c.Filters, other.Filters...)

	return nil
}
