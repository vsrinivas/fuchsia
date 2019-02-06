package testrunner

import (
	"io"
	"time"

	"fuchsia.googlesource.com/tools/runtests"
)

// TestResult is the result of executing a test.
type TestResult struct {
	// Name is the name of the test that was executed.
	Name string

	// Output is the test's combined stdout and stderr streams.
	Output io.Reader

	// Result describes whether the test passed or failed.
	Result runtests.TestResult

	StartTime time.Time
	EndTime   time.Time
}
