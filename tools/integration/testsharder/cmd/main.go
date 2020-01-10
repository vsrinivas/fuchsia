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

	// The filepath to write output to. If unspecified, stdout is used.
	outputFile string

	// The mode in which to run the testsharder.
	mode testsharder.Mode = testsharder.Normal

	// Tags are keys on which to filter environments, which are labeled.
	tags command.StringsFlag

	// The path to the json manifest file containing the tests to mutiply.
	multipliersPath string

	// Maximum number of tests per shard.
	targetShardSize int
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
	flag.StringVar(&outputFile, "output-file", "", "path to a file which will contain the shards as JSON, default is stdout")
	flag.Var(&mode, "mode", "mode in which to run the testsharder (e.g., normal or restricted).")
	flag.Var(&tags, "tag", "environment tags on which to filter; only the tests that match all tags will be sharded")
	flag.StringVar(&multipliersPath, "multipliers", "", "path to the json manifest containing tests to multiply")
	// TODO(olivernewman): Rename or replace this flag once it's no longer set
	// by the recipes, to reflect the fact that it is no longer a hard maximum.
	flag.IntVar(&targetShardSize, "max-shard-size", 0, "target number of tests per shard. If <= 0, will be ignored. Otherwise, tests will be placed into more, smaller shards")
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

	m, err := build.NewModules(buildDir)
	if err != nil {
		return err
	}

	if err = testsharder.ValidateTests(m.TestSpecs(), m.Platforms()); err != nil {
		return err
	}

	opts := &testsharder.ShardOptions{
		Mode: mode,
		Tags: tags,
	}
	shards := testsharder.MakeShards(m.TestSpecs(), opts)

	if multipliersPath != "" {
		multipliers, err := testsharder.LoadTestModifiers(multipliersPath)
		if err != nil {
			return err
		}
		shards = testsharder.MultiplyShards(shards, multipliers)
	}

	testDurations := testsharder.NewTestDurationsMap(m.TestDurations())
	shards = testsharder.WithSize(shards, targetShardSize, testDurations)

	if err := testsharder.ExtractDeps(shards, m.BuildDir()); err != nil {
		return err
	}

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
		return fmt.Errorf("failed to encode shards: %v", err)
	}
	return nil
}
