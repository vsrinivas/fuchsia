// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package batchtester

import (
	"time"
)

type TestStatus string

// Mirrors ResultDB test statuses.
// TODO(olivernewman): Actually use the resultdb proto.
const (
	// The test case has passed.
	Pass TestStatus = "PASS"

	// The test case has failed.
	// Suggests that the code under test is incorrect, but it is also possible
	// that the test is incorrect or it is a flake.
	Fail TestStatus = "FAIL"

	// The test case has crashed during execution.
	// The outcome is inconclusive: the code under test might or might not be
	// correct, but the test+code is incorrect.
	Crash TestStatus = "CRASH"

	// The test case has started, but was aborted before finishing.
	// A common reason: timeout.
	Abort TestStatus = "ABORT"

	// The test case did not execute.
	// Examples:
	// - The execution of the collection of test cases, such as a test
	//   binary, was aborted prematurely and execution of some test cases was
	//   skipped.
	// - The test harness configuration specified that the test case MUST be
	//   skipped.
	Skip TestStatus = "SKIP"
)

type TestResult struct {
	// Name is the name of the test.
	Name string `json:"name"`

	// Status is the status of the test run.
	Status TestStatus `json:"status"`

	// Duration is the time the test took to run.
	Duration time.Duration `json:"duration_nanos"`
}
