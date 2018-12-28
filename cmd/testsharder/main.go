// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"encoding/json"
	"flag"
	"log"
	"os"

	"fuchsia.googlesource.com/tools/build"
	"fuchsia.googlesource.com/tools/testsharder"
)

var (
	// The path to the Fuchsia build output directory.
	fuchsiaBuildDir string

	// The filepath to write output to. If unspecified, stdout is used.
	outputFile string

	// Label is a key on which to filter environments, which are labeled.
	label string
)

func init() {
	flag.StringVar(&fuchsiaBuildDir, "fuchsia-build-dir", "", "(required) path to the fuchsia build output directory")
	flag.StringVar(&outputFile, "output-file", "", "path to a file which will contain the shards as JSON, default is stdout")
	flag.StringVar(&label, "label", "", "environment label on which to filter")
}

func main() {
	flag.Parse()

	if fuchsiaBuildDir == "" {
		log.Fatal("must specify a Fuchsia build output directory")
	}

	// Load spec files.
	pkgs, err := build.LoadPackages(fuchsiaBuildDir)
	if err != nil {
		log.Fatal(err)
	}
	hostTests, err := build.LoadHostTests(fuchsiaBuildDir)
	if err != nil {
		log.Fatal(err)
	}
	specs, err := testsharder.LoadTestSpecs(fuchsiaBuildDir, pkgs, hostTests)
	if err != nil {
		log.Fatal(err)
	}

	// Load test platform environments.
	platforms, err := testsharder.LoadPlatforms(fuchsiaBuildDir)
	if err != nil {
		log.Fatal(err)
	}

	// Verify that the produced specs specify valid test environments.
	if err = testsharder.ValidateTestSpecs(specs, platforms); err != nil {
		log.Fatal(err)
	}

	// Create shards and write them to an output file if specifed, else stdout.
	shards := testsharder.MakeShards(specs, label)
	f := os.Stdout
	if outputFile != "" {
		var err error
		f, err = os.Create(outputFile)
		if err != nil {
			log.Fatalf("unable to create %s: %v", outputFile, err)
		}
		defer f.Close()
	}

	encoder := json.NewEncoder(f)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(&shards); err != nil {
		log.Fatal("failed to encode shards: ", err)
	}
}
