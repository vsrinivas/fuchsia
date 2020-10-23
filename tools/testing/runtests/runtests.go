// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package runtests contains specifics related to the runtests command.
package runtests

import (
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/testparser"
)

// TestResult is the exit result of a test.
type TestResult string

const (
	// TestSummaryFilename is the summary file name expected by the fuchsia
	// recipe module.
	TestSummaryFilename = "summary.json"

	// TestOutputFilename is the default output file name for a test.
	TestOutputFilename = "stdout-and-stderr.txt"

	// TestSuccess represents a passed test.
	TestSuccess TestResult = "PASS"

	// TestFailure represents a failed test.
	TestFailure TestResult = "FAIL"
)

// TestSummary is a summary of a suite of test runs. It represents the output
// file format of a runtests invocation.
type TestSummary struct {
	// Tests is a list of the details of the test runs.
	Tests []TestDetails `json:"tests"`

	// Outputs gives the suite-wide outputs, mapping canonical name of the
	// output to its path.
	Outputs map[string]string `json:"outputs,omitempty"`
}

// DataSink is a data sink exported by the test.
type DataSink struct {
	// Name is the name of the sink.
	Name string `json:"name"`

	// File is the file containing the sink data.
	File string `json:"file"`

	// BuildIDs optionally provides the build IDs relevant to the data (e.g.,
	// those of the modules of a particular test). While `runtests` will not
	// actually provide this information, it is currently the most convenient
	// place to convey this information for non-Fuchsia profiles to covargs.
	// Once build IDs are automatically embedded in any emitted profile,
	// tracked by fxbug.dev/61208, this can be deleted.
	BuildIDs []string `json:"build_ids,omitempty"`
}

// DataSinkMap is mapping from a data sink name to a list of the corresponding
// data sink files.
type DataSinkMap map[string][]DataSink

// TestDetails contains the details of a test run.
type TestDetails struct {
	// Name is the name of the test.
	Name string `json:"name"`

	// GNLabel is label of the test target (with toolchain).
	GNLabel string `json:"gn_label"`

	// OutputFile is a file containing the test's output (stdout/stderr).
	OutputFile string `json:"output_file"`

	// Result is the result of the test.
	Result TestResult `json:"result"`

	// Cases is individual test case results.
	Cases []testparser.TestCaseResult `json:"cases"`

	// DataSinks gives the data sinks attached to a test.
	DataSinks DataSinkMap `json:"data_sinks,omitempty"`

	// StartTime is the UTC time when the test was started.
	StartTime time.Time `json:"start_time"`

	// Duration is how long the test execution took.
	DurationMillis int64 `json:"duration_milliseconds"`

	// IsTestingFailureMode is true iff this test was produced by tefmocheck.
	IsTestingFailureMode bool `json:"is_testing_failure_mode"`
}
