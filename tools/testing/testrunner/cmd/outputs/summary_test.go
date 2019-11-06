// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package outputs_test

import (
	"reflect"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/cmd/outputs"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/lib"
)

func TestSummaryOutput(t *testing.T) {
	start := time.Now()
	inputs := []testrunner.TestResult{{
		Name:      "test_a",
		GNLabel:   "//a/b/c:test_a(//toolchain)",
		Result:    runtests.TestFailure,
		StartTime: start,
		EndTime:   start.Add(10 * time.Millisecond),
	}, {
		Name:    "test_b",
		GNLabel: "//a/b/c:test_b(//toolchain)",
		Result:  runtests.TestSuccess,
	}}

	var output outputs.SummaryOutput
	for _, input := range inputs {
		output.Record(input)
	}

	expectedSummary := runtests.TestSummary{
		Tests: []runtests.TestDetails{{
			Name:           "test_a",
			GNLabel:        "//a/b/c:test_a(//toolchain)",
			OutputFile:     "test_a/stdout-and-stderr.txt",
			Result:         runtests.TestFailure,
			DurationMillis: 10,
		}, {
			Name:       "test_b",
			GNLabel:    "//a/b/c:test_b(//toolchain)",
			OutputFile: "test_b/stdout-and-stderr.txt",
			Result:     runtests.TestSuccess,
			// Unspecified start and end times == 0
			DurationMillis: 0,
		}},
	}

	actualSummary := output.Summary

	if !reflect.DeepEqual(actualSummary, expectedSummary) {
		t.Errorf("got\n%q\nbut wanted\n%q\n", actualSummary, expectedSummary)
	}
}
