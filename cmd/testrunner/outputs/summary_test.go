// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package outputs_test

import (
	"reflect"
	"testing"

	"fuchsia.googlesource.com/tools/cmd/testrunner/outputs"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/testrunner"
)

func TestSummaryOutput(t *testing.T) {
	inputs := []testrunner.TestResult{{
		Name:   "test_a",
		Result: runtests.TestFailure,
	}, {
		Name:   "test_b",
		Result: runtests.TestSuccess,
	}}

	var output outputs.SummaryOutput
	for _, input := range inputs {
		output.Record(input)
	}

	expectedSummary := runtests.TestSummary{
		Tests: []runtests.TestDetails{{
			Name:       "test_a",
			OutputFile: "test_a/stdout-and-stderr.txt",
			Result:     runtests.TestFailure,
		}, {
			Name:       "test_b",
			OutputFile: "test_b/stdout-and-stderr.txt",
			Result:     runtests.TestSuccess,
		}},
	}

	actualSummary := output.Summary

	if !reflect.DeepEqual(actualSummary, expectedSummary) {
		t.Errorf("got\n%q\nbut wanted\n%q\n", actualSummary, expectedSummary)
	}
}
