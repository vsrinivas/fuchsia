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
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testparser"
)

const checkTestNamePrefix = "testing_failure_mode"

func debugPathForCheck(check FailureModeCheck) string {
	return filepath.Join(checkTestNamePrefix, check.Name(), "debug.txt")
}

// RunChecks runs the given checks on the given TestingOutputs.
// A failed test means the Check() returned true. After the first failed test, all
// later Checks() will be skipped. Tests will not be returned for skipped or passed checks.
// Rationale: In order for these bugs to be useful, we want a failure to be associated only with the most
// specific, helpful failure modes, which then get routed to specific bugs. Hence only a single failure
// is returned.
// Rationale for not returning passed tests:
// We want to be able to add many checks without cluttering the output test summary
// with noise. Our flake detection system will identify a test that appears a a failure on
// one run of a swarming task, and then disappears on other runs of that same task as a flake.
func RunChecks(checks []FailureModeCheck, to *TestingOutputs, outputsDir string) ([]runtests.TestDetails, error) {
	var checkTests []runtests.TestDetails
	for _, check := range checks {
		if failed := check.Check(to); !failed {
			continue
		}
		testDetails := runtests.TestDetails{
			Name:                 path.Join(checkTestNamePrefix, check.Name()),
			IsTestingFailureMode: true,
			// Specify an empty slice so it gets serialized to an empty JSON
			// array instead of null.
			Cases:     []testparser.TestCaseResult{},
			Result:    runtests.TestFailure,
			StartTime: time.Now(), // needed by ResultDB
		}
		if len(outputsDir) > 0 {
			outputFile := debugPathForCheck(check)
			testDetails.OutputFiles = []string{outputFile}
			outputFileAbsPath := filepath.Join(outputsDir, outputFile)
			if err := os.MkdirAll(filepath.Dir(outputFileAbsPath), 0o777); err != nil {
				return nil, err
			}
			debugText := fmt.Sprintf(
				"This is a synthetic test that was produced by the tefmocheck tool during post-processing of test results. See https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/testing/tefmocheck/README.md\n%s",
				check.DebugText())
			if err := ioutil.WriteFile(outputFileAbsPath, []byte(debugText), 0o666); err != nil {
				return nil, err
			}
		}
		for _, cof := range check.OutputFiles() {
			relPath, err := filepath.Rel(outputsDir, cof)
			if err != nil {
				return nil, err
			}
			testDetails.OutputFiles = append(testDetails.OutputFiles, relPath)
		}
		checkTests = append(checkTests, testDetails)
		// We run more specific checks first, so it's not useful to run any checks
		// once we have our first failure.
		break
	}
	return checkTests, nil
}
