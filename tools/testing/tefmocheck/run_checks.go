// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testparser"
)

const checkTestNamePrefix = "testing_failure_mode"

func debugPathForCheck(check FailureModeCheck) string {
	return filepath.Join(checkTestNamePrefix, check.Name(), "debug.txt")
}

// RunChecks runs the given checks on the given TestingOutputs.
// It always returns the same list of tests, with one corresponding to each check.
// A failed test means the Check() returned true. After the first failed test, all
// later Checks() will be skipped. Passed tests will be returned for each skipped check.
// Rationale: We want these tests to be useful for bucketing  via Flake Fetcher.
// In order for our infrastructure to consider a test as flaky, we need
// it to sometimes pass and sometimes fail, which means we should always return a test
// even if it passed or was skipped.
// In order for these bugs to be useful, we want a failure to be associated only with the most
// specific, helpful failure modes, which then get routed to specific bugs.
func RunChecks(checks []FailureModeCheck, to *TestingOutputs, outputsDir string) ([]runtests.TestDetails, error) {
	var checkTests []runtests.TestDetails
	anyFailed := false
	for _, check := range checks {
		// We run more specific checks first, so it's not useful to run any checks
		// once we have our first failure.
		testDetails := runtests.TestDetails{
			Name:                 path.Join(checkTestNamePrefix, check.Name()),
			IsTestingFailureMode: true,
			// Specify an empty slice so it gets serialized to an empty JSON
			// array instead of null.
			Cases: []testparser.TestCaseResult{},
		}
		if anyFailed {
			testDetails.Result = runtests.TestSuccess
		} else if anyFailed = check.Check(to); anyFailed {
			testDetails.Result = runtests.TestFailure
			if len(outputsDir) > 0 {
				testDetails.OutputFile = debugPathForCheck(check)
				outputFileAbsPath := filepath.Join(outputsDir, testDetails.OutputFile)
				if err := os.MkdirAll(filepath.Dir(outputFileAbsPath), 0777); err != nil {
					return nil, err
				}
				debugText := fmt.Sprintf(
					"This is a synthetic test that was produced by the tefmocheck tool during post-processing of test results. See https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/testing/tefmocheck/README.md\n%s",
					check.DebugText())
				if err := ioutil.WriteFile(outputFileAbsPath, []byte(debugText), 0666); err != nil {
					return nil, err
				}
			}
		} else {
			testDetails.Result = runtests.TestSuccess
		}
		checkTests = append(checkTests, testDetails)
	}
	return checkTests, nil
}
