// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"encoding/json"
	"flag"
	"log"
	"os"

	"fuchsia.googlesource.com/tools/command"
	"fuchsia.googlesource.com/tools/testsharder"
)

var (
	// The path to the Fuchsia build directory root.
	buildDir string

	// The filepath to write output to. If unspecified, stdout is used.
	outputFile string

	// Tags are keys on which to filter environments, which are labeled.
	tags command.StringsFlag
)

func init() {
	flag.StringVar(&buildDir, "build-dir", "", "path to the fuchsia build directory root (required)")
	flag.StringVar(&outputFile, "output-file", "", "path to a file which will contain the shards as JSON, default is stdout")
	flag.Var(&tags, "tag", "environment tags on which to filter; only the tests that match all tags will be sharded")
}

func main() {
	flag.Parse()

	if buildDir == "" {
		log.Fatal("must specify a Fuchsia build output directory")
	}

	specs, err := testsharder.LoadTestSpecs(buildDir)
	if err != nil {
		log.Fatal(err)
	}
	platforms, err := testsharder.LoadPlatforms(buildDir)
	if err != nil {
		log.Fatal(err)
	}

	// Verify that the produced specs specify valid test environments.
	if err = testsharder.ValidateTestSpecs(specs, platforms); err != nil {
		log.Fatal(err)
	}

	// Create shards and write them to an output file if specifed, else stdout.
	shards := testsharder.MakeShards(specs, tags)
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
