// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

func TestMassTestFailureCheck(t *testing.T) {
	const killerString = "KILLER STRING"
	// Declare as a FailureModeCheck to ensure it implements the interface.
	var c FailureModeCheck = MassTestFailureCheck{MaxFailed: 3}
	summary := runtests.TestSummary{
		Tests: []runtests.TestDetails{
			{Name: "test 1", Result: runtests.TestFailure},
			{Name: "test 1", Result: runtests.TestFailure},
			{Name: "test 2", Result: runtests.TestFailure},
			{Name: "test 3", Result: runtests.TestFailure},
		},
	}
	to := TestingOutputs{
		TestSummary: &summary,
	}
	if c.Check(&to) {
		t.Errorf("MassTestFailureCheck.Check() returned true with only 3 failed tests, expected false")
	}
	summary.Tests = append(summary.Tests, runtests.TestDetails{Name: "test 4", Result: runtests.TestFailure})
	if !c.Check(&to) {
		t.Errorf("MassTestFailureCheck.Check() returned false with 4 failed tests, expected true")
	}
}
