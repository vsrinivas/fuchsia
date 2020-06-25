package tefmocheck

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

const checkTestNamePrefix = "testing_failure_mode"

func debugPathForCheck(outputsDir string, check FailureModeCheck) string {
	return filepath.Join(outputsDir, checkTestNamePrefix, check.Name(), "debug.txt")
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
		}
		if anyFailed {
			testDetails.Result = runtests.TestSuccess
		} else if anyFailed = check.Check(to); anyFailed {
			testDetails.Result = runtests.TestFailure
			if len(outputsDir) > 0 {
				testDetails.OutputFile = debugPathForCheck(outputsDir, check)
				if err := os.MkdirAll(filepath.Dir(testDetails.OutputFile), 0777); err != nil {
					return nil, err
				}
				debugText := fmt.Sprintf(
					"This is a synthetic test that was produced by the tefmocheck tool during post-processing of test results. See https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/testing/tefmocheck/README.md\n%s",
					check.DebugText())
				if err := ioutil.WriteFile(testDetails.OutputFile, []byte(debugText), 0666); err != nil {
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
