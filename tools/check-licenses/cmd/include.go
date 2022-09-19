// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"errors"
	"log"
	"os"
	"path/filepath"
)

// The Include struct contains information about paths and files
// that should be included & merged into the active config file.
type Include struct {
	// Path to the config.json file or root directory.
	Path []string `json:"paths"`
	// When true, recursively find all *.json files starting at the path
	// directory. Attempt to parse and include all found files
	// as config files.
	Recursive bool `json:"recursive"`
	// A simple comment field, used to explain what the given config file
	// should be used for / why it is being included.
	Notes []string `json:"notes"`
	// When true, check-licenses will fail if the path is unavailable.
	// Defaults to false, and allows us to attempt to load in config files
	// from other repositories, but continue with execution if those
	// repos are not available on your local machine.
	Required bool `json:"required"`
}

// Process each "include" entry in this config file.
func (c *CheckLicensesConfig) ProcessIncludes() error {
	// Loop over the Includes field and merge in all config files
	// from that list, recursively.
	if len(c.Includes) > 0 {
		for _, include := range c.Includes {
			if err := c.processInclude(&include); err != nil {
				return err
			}
		}
	}

	return nil
}

func (c *CheckLicensesConfig) processInclude(include *Include) error {
	// Process a single Config include file.
	processPath := func(path string) error {
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
					log.Printf("Failed to create config file for %s: %v.\n",
						path, err)
				}
				return nil
			} else {
				return err
			}
		}
		c.Merge(c2)
		return nil
	}

	// Process a Config include directory, recursively.
	processRecursive := func(path string, info os.FileInfo, err error) error {
		if !info.IsDir() {
			if err := processPath(path); err != nil {
				return err
			}
		}
		return nil
	}

	for _, path := range include.Path {
		if include.Recursive {
			if err := filepath.Walk(path, processRecursive); err != nil {
				return err
			}
		} else {
			if err := processPath(path); err != nil {
				return err
			}
		}
	}
	return nil
}
