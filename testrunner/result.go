package testrunner

import (
	"time"

	"fuchsia.googlesource.com/tools/runtests"
)

// TestResult is the result of executing a test.
type TestResult struct {
	// Name is the name of the test that was executed.
	Name string

	// Result describes whether the test passed or failed.
	Result runtests.TestResult

	Stdout    []byte
	Stderr    []byte
	StartTime time.Time
	EndTime   time.Time
}
