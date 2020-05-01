// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	tefmocheck "go.fuchsia.dev/fuchsia/tools/testing/tefmocheck/lib"
)

func usage() {
	fmt.Printf(`tefmocheck [flags]

Reads inputs from [flags] and writes a JSON formatted summary to stdout.
The summary contains a synthetic test for each supported failure mode.
`)
}

func loadSwarmingTaskSummary(path string) (*tefmocheck.SwarmingTaskSummary, error) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read swarming task summary file %q", path)
	}

	var ret tefmocheck.SwarmingTaskSummary
	if err := json.Unmarshal(data, &ret); err != nil {
		return nil, fmt.Errorf("failed to unmarshal swarming task summary: %w", err)
	}
	return &ret, nil
}

func loadTestSummary(path string) (*runtests.TestSummary, error) {
	if path == "" {
		return &runtests.TestSummary{}, nil
	}
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read test summary file %q", path)
	}

	var ret runtests.TestSummary
	if err := json.Unmarshal(data, &ret); err != nil {
		return nil, fmt.Errorf("failed to unmarshal test summary: %w", err)
	}
	return &ret, nil
}

func main() {
	var help = flag.Bool("help", false, "Whether to show Usage and exit.")
	flag.Usage = usage
	var swarmingSummaryPath = flag.String("swarming-summary-json", "", "Path to the Swarming task summary file. Required.")
	var inputSummaryPath = flag.String("test-summary-json", "", "Path to test summary file. Optional.")
	// TODO(garymm): Store flags in vars.
	flag.String("swarming-output", "", "Path to a file containing the stdout and stderr of the Swarming task. Optional.")
	flag.String("syslog", "", "Path to a file containing the syslog. Optional.")
	flag.String("serial-log", "", "Path to a file containing the serial log. Optional.")
	flag.Parse()

	if *help || flag.NArg() > 0 || *swarmingSummaryPath == "" {
		flag.Usage()
		flag.PrintDefaults()
		if *help {
			return
		}
		os.Exit(64)
	}

	// TODO(garymm): Use the return value to run checks.
	_, err := loadSwarmingTaskSummary(*swarmingSummaryPath)
	if err != nil {
		log.Fatal(err)
	}

	outputSummary, err := loadTestSummary(*inputSummaryPath)
	if err != nil {
		log.Fatal(err)
	}

	// TODO(garymm): Run checks and add tests to outputSummary.

	jsonOutput, err := json.MarshalIndent(outputSummary, "", "  ")
	if err != nil {
		log.Fatalf("failed to marshal output test summary: %v", err)
	}
	os.Stdout.Write(jsonOutput)
	fmt.Println("") // Terminate output with new line.
}
