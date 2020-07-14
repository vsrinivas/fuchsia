// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

func TestNoTestsRanCheck(t *testing.T) {
	// Declare as a FailureModeCheck to ensure it implements the interface.
	var c FailureModeCheck = NoTestsRanCheck{}
	summary := runtests.TestSummary{
		Tests: []runtests.TestDetails{},
	}
	to := TestingOutputs{
		TestSummary: &summary,
	}
	if !c.Check(&to) {
		t.Errorf("NoTestsRanCheck.Check() returned false with no tests, expected true")
	}
	summary.Tests = append(summary.Tests, runtests.TestDetails{Result: runtests.TestFailure})
	if c.Check(&to) {
		t.Errorf("NoTestsRanCheck.Check() returned true with one test run, expected false")
	}
}
