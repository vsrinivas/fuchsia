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
	"time"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/flagmisc"
)

var (
	// The path to the Fuchsia build directory root.
	buildDir string

	// The filepath to write output to. If unspecified, stdout is used.
	outputFile string

	// The mode in which to run the testsharder.
	mode testsharder.Mode = testsharder.Normal

	// Tags are keys on which to filter environments, which are labeled.
	tags flagmisc.StringsValue

	// The path to the json manifest file containing the tests to modify.
	modifiersPath string

	// Target number of tests per shard.
	targetTestCount int

	// Target duration per shard.
	targetDurationSecs int

	// Maximum number of shards per environment.
	maxShardsPerEnvironment int
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
	flag.StringVar(&modifiersPath, "modifiers", "", "path to the json manifest containing tests to modify")
	flag.IntVar(&targetDurationSecs, "target-duration-secs", 0, "approximate duration that each shard should run in")
	flag.IntVar(&maxShardsPerEnvironment, "max-shards-per-env", 8, "maximum shards allowed per environment. If <= 0, no max will be set")
	// Despite being a misnomer, this argument is still called -max-shard-size
	// for legacy reasons. If it becomes confusing, we can create a new
	// target_test_count fuchsia.proto field and do a soft transition with the
	// recipes to start setting the renamed argument instead.
	flag.IntVar(&targetTestCount, "max-shard-size", 0, "target number of tests per shard. If <= 0, will be ignored. Otherwise, tests will be placed into more, smaller shards")
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

	targetDuration := time.Duration(targetDurationSecs) * time.Second
	if targetTestCount > 0 && targetDuration > 0 {
		return fmt.Errorf("max-shard-size and target-duration-secs cannot both be set")
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

	testDurations := testsharder.NewTestDurationsMap(m.TestDurations())

	var modifiers []testsharder.TestModifier
	if modifiersPath != "" {
		modifiers, err = testsharder.LoadTestModifiers(modifiersPath)
		if err != nil {
			return err
		}
	}

	shards, err = testsharder.ShardAffected(shards, modifiers)
	if err != nil {
		return err
	}

	shards, err = testsharder.MultiplyShards(shards, modifiers, testDurations, targetDuration, targetTestCount)
	if err != nil {
		return err
	}

	shards = testsharder.WithTargetDuration(shards, targetDuration, targetTestCount, maxShardsPerEnvironment, testDurations)

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
