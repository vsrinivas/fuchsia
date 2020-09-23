// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"

	checklicenses "go.fuchsia.dev/fuchsia/tools/check-licenses"
)

var (
	config checklicenses.Config

	configFile = flag.String("config_file", "tools/check-licenses/config/config.json", "Location of config.json.")
	target     = flag.String("target", "Options: {all, <target>}", "Analyze the dependency tree of a specific GN build target.")
)

func validateArgs() {
	_, err := os.Stat(*configFile)
	if os.IsNotExist(err) {
		log.Fatalf("Config file \"%v\" does not exist!", *configFile)
	}

	if *target != "Options: {all, <target>}" {
		log.Printf("WARNING: Flag \"target\" was set to \"%v\", but that flag is currently unsupported. check-licenses will parse the full directory tree.\n", *target)
		*target = "Options: {all, <target>}"
	}
	config.Target = *target

	if err := config.Init(configFile); err != nil {
		log.Fatalf("Failed to initialize config: %v", err)
	}
}

func main() {
	flag.Parse()
	validateArgs()

	if err := checklicenses.Walk(&config); err != nil {
		log.Fatalf("Failed to analyze the given directory: %v", err)
	}
}
