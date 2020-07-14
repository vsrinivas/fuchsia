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
	var swarmingOutputPath = flag.String("swarming-output", "", "Path to a file containing the stdout and stderr of the Swarming task. Optional.")
	var syslogPath = flag.String("syslog", "", "Path to a file containing the syslog. Optional.")
	var serialLogPath = flag.String("serial-log", "", "Path to a file containing the serial log. Optional.")
	var outputsDir = flag.String("outputs-dir", "", "If set, will produce text output files for the produced tests in this dir. Optional.")
	flag.Parse()

	if *help || flag.NArg() > 0 || *swarmingSummaryPath == "" {
		flag.Usage()
		flag.PrintDefaults()
		if *help {
			return
		}
		os.Exit(64)
	}

	swarmingSummary, err := loadSwarmingTaskSummary(*swarmingSummaryPath)
	if err != nil {
		log.Fatal(err)
	}

	inputSummary, err := loadTestSummary(*inputSummaryPath)
	if err != nil {
		log.Fatal(err)
	}

	var serialLog []byte
	if *serialLogPath != "" {
		serialLog, err = ioutil.ReadFile(*serialLogPath)
		if err != nil {
			log.Fatalf("failed to read serial log from %s: %e", *serialLogPath, err)
		}
	}

	var swarmingOutput []byte
	if *swarmingOutputPath != "" {
		swarmingOutput, err = ioutil.ReadFile(*swarmingOutputPath)
		if err != nil {
			log.Fatalf("failed to read swarming output from %s: %e", *swarmingOutputPath, err)
		}
	}

	var syslog []byte
	if *syslogPath != "" {
		syslog, err = ioutil.ReadFile(*syslogPath)
		if err != nil {
			log.Fatalf("failed to read syslog from %s: %e", *syslogPath, err)
		}
	}

	testingOutputs := tefmocheck.TestingOutputs{
		TestSummary:     inputSummary,
		SwarmingSummary: swarmingSummary,
		SerialLog:       serialLog,
		SwarmingOutput:  swarmingOutput,
		Syslog:          syslog,
	}

	// These should be ordered from most specific to least specific. If an earlier
	// check finds a failure mode, then we skip running later checks because we assume
	// they'll add no useful information.
	checks := []tefmocheck.FailureModeCheck{}
	checks = append(checks, tefmocheck.StringInLogsChecks()...)
	checks = append(checks, tefmocheck.MassTestFailureCheck{MaxFailed: 5})
	// TaskStateChecks should go toward the end, since they're not very specific.
	checks = append(checks, tefmocheck.TaskStateChecks...)
	// No tests being run is only an issue if the task didn't fail for another
	// reason, since conditions handled by many other checks can result in a
	// missing summary.json. So run this check last.
	checks = append(checks, tefmocheck.NoTestsRanCheck{})

	checkTests, err := tefmocheck.RunChecks(checks, &testingOutputs, *outputsDir)
	if err != nil {
		log.Fatalf("failed to run checks: %v", err)
	}

	inputSummary.Tests = append(inputSummary.Tests, checkTests...)
	jsonOutput, err := json.MarshalIndent(inputSummary, "", "  ")
	if err != nil {
		log.Fatalf("failed to marshal output test summary: %v", err)
	}
	os.Stdout.Write(jsonOutput)
	fmt.Println("") // Terminate output with new line.
}
