// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements the perf test results schema.
//
// See https://fuchsia.googlesource.com/docs/+/master/development/benchmarking/results_schema
// for more details.

package benchmarking

import (
	"encoding/json"
	"io"
)

// Unit represents the units of a test's measured values.
type Unit string

// The set of valid Unit constants.
//
// This should be kept in sync with the list of supported units in the results schema docs
// linked at the top of this file.
const (
	// Time-based units.
	Nanoseconds  Unit = "nanoseconds"
	Milliseconds      = "milliseconds"

	// Size-based units.
	BytesPerSecond = "bytes/second"
)

// TestCaseResults represents the results for a single test case.
//
// See the link at the top of this file for documentation.
type TestCaseResults struct {
	Label      string    `json:"label"`
	TestSuite  string    `json:"test_suite"`
	Unit       Unit      `json:"unit"`
	Values     []float64 `json:"values"`
	SplitFirst bool      `json:"split_first"`
}

// TestResultsFile represents the results produced by individual benchmarks.
//
// Example usage:
//
//     results := new(TestResultsFile)
//     results.Add(&TestCaseResults{
//         Label: "test case",
//         TestSuite: "test suite",
//         Unit:  Nanoseconds,
//         Values: []float64{100, 200, 300},
//         SplitFirst: true,
//     })
//
//     _ := results.Encode(outputFile)
//
type TestResultsFile []*TestCaseResults

// Add a TestCaseResults entry to the file.
func (f *TestResultsFile) Add(results *TestCaseResults) {
	*f = append(*f, results)
}

// Encode writes the results file as JSON to the given io.Writer.
func (f *TestResultsFile) Encode(w io.Writer) error {
	encoder := json.NewEncoder(w)
	encoder.SetIndent("", "  ")
	return encoder.Encode(f)
}

// DecodeTestResultsFile parses a TestResultsFile from the input.
func DecodeTestResultsFile(js []byte) (*TestResultsFile, error) {
	file := new(TestResultsFile)
	err := json.Unmarshal(js, file)
	return file, err
}
