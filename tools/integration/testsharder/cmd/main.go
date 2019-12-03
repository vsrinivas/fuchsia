// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/command"
)

var (
	// The path to the Fuchsia build directory root.
	buildDir string

	// The path to the Fuchsia checkout root.
	checkoutDir string

	// The filepath to write output to. If unspecified, stdout is used.
	outputFile string

	// The mode in which to run the testsharder.
	mode testsharder.Mode = testsharder.Normal

	// Tags are keys on which to filter environments, which are labeled.
	tags command.StringsFlag

	// The path to the json manifest file containing the tests to mutiply.
	multipliersPath string

	// Maximum number of tests per shard.
	maxShardSize int
)

func usage() {
	fmt.Printf(`testsharder [flags]

Shards tests produced by a build.
For more information on the modes in which the testsharder may be run, see
See https://go.fuchsia.dev/fuchsia/tools/+/master/testsharder/mode.go.
`)
}

func init() {
	flag.StringVar(&buildDir, "build-dir", "", "path to the fuchsia build directory root (required)")
	// TODO(olivernewman): Make -checkout-dir required after recipes set it.
	flag.StringVar(&checkoutDir, "checkout-dir", "", "path to the fuchsia checkout root")
	flag.StringVar(&outputFile, "output-file", "", "path to a file which will contain the shards as JSON, default is stdout")
	flag.Var(&mode, "mode", "mode in which to run the testsharder (e.g., normal or restricted).")
	flag.Var(&tags, "tag", "environment tags on which to filter; only the tests that match all tags will be sharded")
	flag.StringVar(&multipliersPath, "multipliers", "", "path to the json manifest containing tests to multiply")
	flag.IntVar(&maxShardSize, "max-shard-size", 0, "maximum number of tests per shard. If <= 0, will be ignored. Otherwise, tests will be placed into more, smaller shards")
	flag.Usage = usage
}

func main() {
	flag.Parse()
	if err := execute(); err != nil {
		log.Fatal(err)
	}
}

func execute() error {

	if buildDir == "" {
		return fmt.Errorf("must specify a Fuchsia build output directory")
	}

	modCtx, err := build.NewModules(buildDir)
	if err != nil {
		return err
	}

	if err = testsharder.ValidateTests(modCtx.TestSpecs(), modCtx.Platforms()); err != nil {
		return err
	}

	// TODO(fxbug.dev/37955): Fetch test specs via modCtx once the
	// `DepsFile`-related logic has been separated out into a separate
	// utility.
	specs, err := testsharder.LoadTestSpecs(modCtx.BuildDir())
	if err != nil {
		return err
	}

	// Create shards and write them to an output file if specifed, else stdout.
	shards := testsharder.MakeShards(specs, mode, tags)
	if multipliersPath != "" {
		multipliers, err := testsharder.LoadTestModifiers(multipliersPath)
		if err != nil {
			return err
		}
		shards = testsharder.MultiplyShards(shards, multipliers)
	}
	shards = testsharder.WithMaxSize(shards, maxShardSize)
	f := os.Stdout
	if outputFile != "" {
		var err error
		f, err = os.Create(outputFile)
		if err != nil {
			return fmt.Errorf("unable to create %s: %v", outputFile, err)
		}
		defer f.Close()
	}

	encoder := json.NewEncoder(f)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(&shards); err != nil {
		return fmt.Errorf("failed to encode shards: ", err)
	}
	return nil
}
