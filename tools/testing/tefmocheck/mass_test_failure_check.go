// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

type MassTestFailureCheck struct {
	MaxFailed int
}

func (c MassTestFailureCheck) Check(to *TestingOutputs) bool {
	failedTests := make(map[string]bool)
	for _, test := range to.TestSummary.Tests {
		if _, ok := failedTests[test.Name]; !ok && test.Result == runtests.TestFailure {
			failedTests[test.Name] = true
			if len(failedTests) > c.MaxFailed {
				return true
			}
		}
	}
	return false
}

func (c MassTestFailureCheck) Name() string {
	return fmt.Sprintf("more_than_%d_tests_failed", c.MaxFailed)
}

func (c MassTestFailureCheck) DebugText() string {
	return fmt.Sprintf(
		`More than %d tests failed.
It's unlikely that any one test is to blame. Rather the device or OS probably had a low level problem.`,
		c.MaxFailed)
}
