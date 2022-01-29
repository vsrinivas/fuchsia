// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package testrunner handles specifics related to the testrunner tool.
package testrunner

import (
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

// TestResult is the result of executing a test.
type TestResult struct {
	// Name is the name of the test that was executed.
	Name string

	// GNLabel is the label (with toolchain) for the test target.
	GNLabel string

	// Result describes whether the test passed or failed.
	Result runtests.TestResult

	// FailReason is the error message from a failed test.
	FailReason string

	// Cases describes individual test cases.
	Cases []runtests.TestCaseResult

	// DataSinks gives the data sinks attached to a test.
	DataSinks runtests.DataSinkReference

	// RunIndex is the index of this test run among all the runs of the same test.
	RunIndex int

	// The combined stdout and stderr from this test.
	Stdio []byte

	// The relative paths to the output files of the test.
	OutputFiles []string
	// The directory where the OutputFiles live.
	OutputDir string

	// Whether the test is affected by the change under test. This will only be
	// set for tests running within tryjobs.
	Affected bool `json:"affected,omitempty"`

	StartTime time.Time
	EndTime   time.Time
}

// Passed indicates whether the test completed successfully. This will be false
// if the test timed out or failed.
func (r TestResult) Passed() bool {
	return r.Result == runtests.TestSuccess
}

func (r TestResult) Duration() time.Duration {
	return r.EndTime.Sub(r.StartTime)
}
