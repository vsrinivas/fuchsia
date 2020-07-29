// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import "go.fuchsia.dev/fuchsia/tools/build"

// RunAlgorithm descibes how to run a test using the test's `Runs` field.
type RunAlgorithm string

const (
	// StopOnSuccess means to try the test up to `Runs` times
	// and to break on the first success.
	StopOnSuccess RunAlgorithm = "STOP_ON_SUCCESS"
	// KeepGoing means to run the test for as many times as `Runs`
	// regardless of the result of each test run.
	KeepGoing RunAlgorithm = "KEEP_GOING"
)

// Test is a struct used to hold information about a build.Test and the number
// of times to run it.
type Test struct {
	build.Test

	// Runs is the number of times this test should be run.
	Runs int `json:"runs,omitempty"`

	// RunAlgorithm determines how `Runs` will be used to run the test.
	RunAlgorithm RunAlgorithm `json:"run_algorithm",omitempty"`
}

func (t *Test) applyModifier(m TestModifier) {
	if m.MaxAttempts > 0 {
		t.Runs = m.MaxAttempts
		if t.Runs > 1 {
			t.RunAlgorithm = StopOnSuccess
		} else {
			t.RunAlgorithm = ""
		}
	}
}

func (t *Test) minRequiredRuns() int {
	if t.RunAlgorithm == KeepGoing {
		return t.Runs
	}
	return 1
}
