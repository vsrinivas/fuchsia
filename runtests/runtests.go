// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runtests

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

// TestDetails contains the details of a test run.
type TestDetails struct {
	// Name is the name of the test.
	Name string `json:"name"`

	// OutputFile is a file containing the test's output (stdout/stderr).
	OutputFile string `json:"output_file"`

	// Result is the result of the test.
	Result TestResult `json:"result"`

	// DataSinks gives the data sinks attached to a test.
	DataSinks map[string][]struct {
		Name string `json:"name"`
		File string `json:"file"`
	} `json:"data_sinks,omitempty"`
}
