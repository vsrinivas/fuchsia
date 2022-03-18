// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
	/*
		"go.fuchsia.dev/fuchsia/tools/check-licenses/filetree"
		"go.fuchsia.dev/fuchsia/tools/check-licenses/result"
	*/)

// Config file version 2.
// TODO: Rename this file to "config.go" once v1 is turned off.
// ============================================================

type Include struct {
	Path     []string `json:"paths"`
	Notes    []string `json:"notes"`
	Required bool     `json:"required"`
}

type CheckLicensesConfig struct {
	Includes []Include `json:"includes"`

	File    *file.FileConfig       `json:"file"`
	License *license.LicenseConfig `json:"license"`
	Project *project.ProjectConfig `json:"project"`
	/*
		FileTree *filetree.FileTreeConfig `json:"filetree"`
		Result   *result.ResultConfig     `json:"result"`
	*/

	Target string `json:"target"`
}

func NewCheckLicensesConfig(path string) (*CheckLicensesConfig, error) {
	b, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("Failed to read config file [%v]: %v\n", path, err)
	}

	c, err := NewCheckLicensesConfigJson(string(b))
	if err != nil {
		return nil, fmt.Errorf("Failed to parse JSON config file [%v]: %v\n", path, err)
	}
	return c, nil
}

func NewCheckLicensesConfigJson(configJson string) (*CheckLicensesConfig, error) {
	configJson = strings.ReplaceAll(configJson, "{FUCHSIA_DIR}", os.Getenv("FUCHSIA_DIR"))
	c := &CheckLicensesConfig{
		File:    &file.FileConfig{},
		License: &license.LicenseConfig{},
		Project: &project.ProjectConfig{},
		/*
			FileTree: &filetree.FileTreeConfig{},
			Result:   &result.ResultConfig{},
		*/
	}

	d := json.NewDecoder(strings.NewReader(configJson))
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
					if include.Required {
						return nil, err
					} else {
						fmt.Printf("Failed to create config file for %s: %v.\n", path, err)
						continue
					}
				}
				c.Merge(c2)
			}
		}
	}

	return c, nil
}

func (c *CheckLicensesConfig) Merge(other *CheckLicensesConfig) {
	c.File.Merge(other.File)
	c.License.Merge(other.License)
	/*
		c.FileTree.Merge(other.FileTree)
		c.Project.Merge(other.Project)
		c.Result.Merge(other.Result)
	*/

	c.Includes = append(c.Includes, other.Includes...)
}
