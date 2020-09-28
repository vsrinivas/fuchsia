// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"
	"strings"

	checklicenses "go.fuchsia.dev/fuchsia/tools/check-licenses"
)

var config checklicenses.Config

var (
	configFile = flag.String("config_file", "tools/check-licenses/config/config.json", "Location of config.json.")

	skipDirs                     = flag.String("skip_dirs", "", "Comma separated list of directory names to skip when traversing a directory tree. This arg is added to the list of skipdirs in the config file.")
	skipFiles                    = flag.String("skip_files", "", "Comma separated list of file names to skip when traversing a directory tree. This arg is added to the list of skipfiles in the config file.")
	licensePatternDir            = flag.String("license_pattern_dir", "", "Location of directory containing pattern files for all licenses that may exist in the given code base.")
	baseDir                      = flag.String("base_dir", "", "Root location to begin directory traversal.")
	target                       = flag.String("target", "", "Analyze the dependency tree of a specific GN build target.")
	logLevel                     = flag.Int("log_level", 0, "Log level, see https://godoc.org/github.com/golang/glog for more info.")
	exitOnProhibitedLicenseTypes = flag.Bool("exit_on_prohibited_license_types", true, "If true, exits if it encounters a prohibited license type.")
	prohibitedLicenseTypes       = flag.String("prohibited_license_types", "", "Comma separated list of license types that are prohibited. This arg is added to the list of prohibitedLicenseTypes in the config file.")
)

func validateArgs() {
	if _, err := os.Stat(*configFile); os.IsNotExist(err) {
		log.Fatalf("Config file \"%v\" does not exist!", *configFile)
	}
	if err := config.Init(configFile); err != nil {
		log.Fatalf("Failed to initialize config: %v", err)
	}

	if *skipDirs != "" {
		split := strings.Split(*skipDirs, ",")
		for _, s := range split {
			if s != "" {
				config.SkipDirs = append(config.SkipDirs, s)
			}
		}
	}

	if *skipFiles != "" {
		split := strings.Split(*skipFiles, ",")
		for _, s := range split {
			if s != "" {
				config.SkipFiles = append(config.SkipFiles, s)
			}
		}
	}

	if *prohibitedLicenseTypes != "" {
		split := strings.Split(*prohibitedLicenseTypes, ",")
		for _, s := range split {
			if s != "" {
				config.ProhibitedLicenseTypes = append(config.ProhibitedLicenseTypes, s)
			}
		}
	}

	config.ExitOnProhibitedLicenseTypes = *exitOnProhibitedLicenseTypes

	if *licensePatternDir != "" {
		if info, err := os.Stat(*licensePatternDir); os.IsNotExist(err) && info.IsDir() {
			log.Fatalf("License pattern directory path \"%v\" does not exist!", *licensePatternDir)
		}
		config.LicensePatternDir = *licensePatternDir
	}

	if *baseDir != "" {
		if info, err := os.Stat(*baseDir); os.IsNotExist(err) && info.IsDir() {
			log.Fatalf("Base directory path \"%v\" does not exist!", *baseDir)
		}
		config.BaseDir = *baseDir
	}

	if *target != "" {
		log.Printf("WARNING: Flag \"target\" was set to \"%v\", but that flag is currently unsupported. check-licenses will parse the full directory tree.\n", *target)
		//TODO: enable target-based dependency traversal
		//config.Target = *target
	}

	//TODO: incorporate https://godoc.org/github.com/golang/glog
	//and configure it using the logLevel argument
}

func main() {
	flag.Parse()
	validateArgs()

	if err := checklicenses.Walk(&config); err != nil {
		log.Fatalf("Failed to analyze the given directory: %v", err)
	}
}
