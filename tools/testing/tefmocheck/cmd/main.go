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
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tefmocheck"
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
	if ret.Results == nil {
		return nil, fmt.Errorf("swarming task summary did not contain top level `results`. Loaded from path: %s", path)
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
	help := flag.Bool("help", false, "Whether to show Usage and exit.")
	flag.Usage = usage
	swarmingSummaryPath := flag.String("swarming-summary-json", "", "Path to the Swarming task summary file. Required.")
	swarmingHost := flag.String("swarming-host", "", "Swarming server host. Optional.")
	inputSummaryPath := flag.String("test-summary-json", "", "Path to test summary file. Optional.")
	swarmingOutputPath := flag.String("swarming-output", "", "Path to a file containing the stdout and stderr of the Swarming task. Optional.")
	syslogPath := flag.String("syslog", "", "Path to a file containing the syslog. Optional.")
	serialLogPath := flag.String("serial-log", "", "Path to a file containing the serial log. Optional.")
	outputsDir := flag.String("outputs-dir", "", "If set, will produce text output files for the produced tests in this dir. Optional.")
	jsonOutput := flag.String("json-output", "", "Output summary.json to this path.")
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
	swarmingSummary.Host = *swarmingHost

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
	var swarmingOutputPerTest []tefmocheck.TestLog
	if *swarmingOutputPath != "" {
		swarmingOutput, err = ioutil.ReadFile(*swarmingOutputPath)
		if err != nil {
			log.Fatalf("failed to read swarming output from %s: %e", *swarmingOutputPath, err)
		}
		var perTestLogDir string
		if *outputsDir != "" {
			perTestLogDir = filepath.Join(*outputsDir, "per-test")
		}
		swarmingOutputPerTest, err = tefmocheck.SplitTestLogs(swarmingOutput, filepath.Base(*swarmingOutputPath), perTestLogDir)
		if err != nil {
			log.Fatalf("failed to split swarming output into per-test logs: %s", err)
		}
	}
	// inputSummary is empty if -test-summary-json is not specified. This happens
	// when the recipe detects no summary.json exists.
	if *outputsDir != "" && *inputSummaryPath != "" {
		for i := range swarmingOutputPerTest {
			test := &inputSummary.Tests[i]
			testLog := &swarmingOutputPerTest[i]
			if test.Name != testLog.TestName {
				log.Fatalf("swarmingOutputPerTest[%d].TestName != inputSummary.Tests[%d] (%q vs %q)", i, i, testLog.TestName, test.Name)
			}
			relPath, err := filepath.Rel(*outputsDir, testLog.FilePath)
			if err != nil {
				log.Fatal(err)
			}
			test.OutputFiles = append(test.OutputFiles, relPath)
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
		TestSummary:           inputSummary,
		SwarmingSummary:       swarmingSummary,
		SerialLog:             serialLog,
		SwarmingOutput:        swarmingOutput,
		SwarmingOutputPerTest: swarmingOutputPerTest,
		Syslog:                syslog,
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
	outFile := os.Stdout
	if *jsonOutput != "" {
		outFile, err = osmisc.CreateFile(*jsonOutput)
		if err != nil {
			log.Fatalf("failed to create output file: %s", err)
		}
	}
	summaryBytes, err := json.MarshalIndent(inputSummary, "", "  ")
	if err != nil {
		log.Fatalf("failed to marshal output test summary: %s", err)
	}
	summaryBytes = append(summaryBytes, []byte("\n")...) // Terminate output with new line.
	if _, err := outFile.Write(summaryBytes); err != nil {
		log.Fatalf("failed to write summary: %s", err)
	}
}
