// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"runtime/trace"
	"time"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/directory"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/license"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/project"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/result"
	"go.fuchsia.dev/fuchsia/tools/check-licenses/result/world"
)

// Execute kicks-off the check-licenses runthrough.
// It is assumed that all configuration settings have been set before this is called.
func Execute(ctx context.Context) error {
	start := time.Now()

	// Initialize all package configs.
	startInitialize := time.Now()
	log.Print("Initializing... ")
	if err := initialize(); err != nil {
		log.Println("Error!")
		return err
	}
	log.Printf("Done. [%v]\n", time.Since(startInitialize))

	// Traverse the repository, generating a tree of Dictionary and File objects in memory.
	r := trace.StartRegion(ctx, "directory.NewDirectory("+Config.FuchsiaDir+")")
	startDirectory := time.Now()
	log.Print("Discovering files and folders... ")
	_, err := directory.NewDirectory(Config.FuchsiaDir, nil)
	if err != nil {
		log.Println("Error!")
		return err
	}
	log.Printf("Done. [%v]\n", time.Since(startDirectory))
	r.End()

	// If we plan on generating an output notice file:
	// Filter out the projects that we don't care about (absent from the build graph).
	if Config.OutputLicenseFile {
		r := trace.StartRegion(ctx, "cmd.FilterProjects("+Config.Target+")")
		startFilter := time.Now()
		target := Config.Target
		if target == "" {
			target = fmt.Sprintf("%v.%v",
				Config.BuildInfoProduct,
				Config.BuildInfoBoard)
		}
		log.Printf("Filtering out projects that are not in the build graph for [%v]...",
			target)
		if err := FilterProjects(); err != nil {
			log.Println("Error!")
			return err
		}
		log.Printf("Done. [%v]\n", time.Since(startFilter))
		r.End()
	} else {
		project.FilteredProjects = project.AllProjects
	}

	// Analyze the remaining projects, and keep track of all found license texts.
	r = trace.StartRegion(ctx, "project.AnalyzeLicenses")
	startAnalyze := time.Now()
	log.Printf("Searching for license texts [%v projects]... ", len(project.FilteredProjects))
	err = project.AnalyzeLicenses()
	if err != nil {
		log.Println("Error!")
		return err
	}
	log.Printf("Done. [%v]\n", time.Since(startAnalyze))
	r.End()

	// Save the resulting NOTICE file (if necessary), all config files
	// and execution metrics to the output directory.
	// Also perform checks to ensure the repository is in a good state.
	r = trace.StartRegion(ctx, "result.SaveResults")
	startSaveResults := time.Now()
	log.Print("Saving results... ")
	var s string
	s, err = result.SaveResults(Config, Metrics)
	if err != nil {
		return err
	}
	log.Printf("Done. [%v]\n", time.Since(startSaveResults))
	r.End()

	// Done.
	log.Printf("\nTotal runtime: %v\n============\n", time.Since(start))
	log.Println(s)
	return nil
}

// Initialize each go package with their updated config files.
func initialize() error {
	if err := file.Initialize(Config.File); err != nil {
		return err
	}
	if err := license.Initialize(Config.License); err != nil {
		return err
	}
	if err := project.Initialize(Config.Project); err != nil {
		return err
	}
	if err := directory.Initialize(Config.Directory); err != nil {
		return err
	}
	if err := result.Initialize(Config.Result); err != nil {
		return err
	}
	if err := world.Initialize(Config.World); err != nil {
		return err
	}

	// Save the config file to the out directory (if defined).
	if b, err := json.MarshalIndent(Config, "", "  "); err != nil {
		return err
	} else {
		plusFile("_config.json", b)
	}

	return nil
}
