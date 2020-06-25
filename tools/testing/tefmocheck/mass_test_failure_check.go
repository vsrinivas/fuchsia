package tefmocheck

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

type MassTestFailureCheck struct {
	MaxFailed int
}

func (c MassTestFailureCheck) Check(to *TestingOutputs) bool {
	numFailed := 0
	for _, test := range to.TestSummary.Tests {
		if test.Result == runtests.TestFailure {
			numFailed++
			if numFailed > c.MaxFailed {
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
