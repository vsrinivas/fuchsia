// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
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
	ConfigVars map[string]string
)

func init() {
	ConfigVars = make(map[string]string)
}

type CheckLicensesConfig struct {
	LogLevel int `json:"logLevel"`

	FuchsiaDir string `json:"fuchsiaDir"`
	OutDir     string `json:"outDir"`

	Includes []Include `json:"includes"`

	File      *file.FileConfig           `json:"file"`
	License   *license.LicenseConfig     `json:"license"`
	Project   *project.ProjectConfig     `json:"project"`
	Directory *directory.DirectoryConfig `json:"directory"`
	Result    *result.ResultConfig       `json:"result"`
	World     *world.WorldConfig         `json:"world"`

	Target string `json:"target"`

	// The SDK version for the current workspace.
	BuildInfoVersion string `json:"buildInfoVersion"`
	// The product currently set in the fx args for the local workspace.
	BuildInfoProduct string `json:"buildInfoProduct"`
	// The board currently set in the fx args for the local workspace.
	BuildInfoBoard string `json:"buildInfoBoard"`

	Filters []string

	OutputLicenseFile bool `json:"outputLicenseFile"`
}

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
	// TODO: Uncomment once "Filter" fields are removed from all config files.
	//d.DisallowUnknownFields()
	if err := d.Decode(c); err != nil {
		return nil, err
	}

	// Loop over the Includes field and merge in all config files
	// from that list, recursively.
	if len(c.Includes) > 0 {
		for _, include := range c.Includes {
			for _, path := range include.Path {
				c2, err := NewCheckLicensesConfig(path)
				// If we get an error loading the config file,
				// it may be because a given submodule isn't
				// available on your machine (e.g. //vendor/google).
				//
				// Only error out if this config section is marked
				// as "required".
				if err != nil {
					if errors.Is(err, os.ErrNotExist) && !include.Required {
						if c.LogLevel > 0 {
							log.Printf("Failed to create config file for %s: %v.\n", path, err)
						}
						continue
					} else {
						return nil, err
					}
				}
				c.Merge(c2)
			}
		}
	}

	if err := c.ProcessIncludes(); err != nil {
		return nil, err
	}

	return c, nil
}

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

	c.Filters = append(c.Filters, other.Filters...)
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

	return nil
}
