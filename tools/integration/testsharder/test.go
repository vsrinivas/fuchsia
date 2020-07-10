// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import "go.fuchsia.dev/fuchsia/tools/build/lib"

// Test is a struct used to hold information about a build.Test and the number
// of times to run it.
type Test struct {
	build.Test

	// Runs is the number of times this test should be run.
	Runs int `json:"runs,omitempty"`

	// MaxAttempts is the max number of times to run this test if it fails.
	// This is the max attempts per run as specified by the `Runs` field.
	MaxAttempts int `json:"max_attempts,omitempty"`
}

func (t *Test) applyModifier(m TestModifier) {
	if m.MaxAttempts > 0 {
		t.MaxAttempts = m.MaxAttempts
	}
}
