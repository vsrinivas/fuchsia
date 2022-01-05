// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"regexp"
	"strings"
)

var (
	trfTestPreamblePattern = regexp.MustCompile(`^Running test 'fuchsia-pkg:\/\/.*$`)
	// ex: "[PASSED]	InlineDirTest.InlineDirPino"
	trfTestCasePattern = regexp.MustCompile(`^\[(PASSED|FAILED|INCONCLUSIVE|TIMED_OUT|ERROR|SKIPPED)\]\t(.*)$`)
	// ex: "[stderr - NodeManagerTest.TruncateExceptionCase]"
	// Note that we don't try to parse for end of stderr, because the trf stdout always prints the real stderr msg
	// after the matched trfTestCaseStderr pattern one line at a time.
	// For ex:
	//   [stderr - suite1.case1]
	//   first line of error msg from case1
	//   [stderr - suite1.case2]
	//   first line of error msg from case2
	//   [stderr - suite1.case1]
	//   second line of error msg from case1
	//   [stderr - suite1.case2]
	//   second line of error msg from case2
	trfTestCaseStderr = regexp.MustCompile(`^\[stderr - (?P<test_name>.*?)\]$`)
)

// Parse tests run by the Test Runner Framework (TRF)
func parseTrfTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	testCases := make(map[string]TestCaseResult)
	errorMessages := make(map[string]*strings.Builder)

	currentTestName := ""
	foundStderr := false
	for _, line := range lines {
		line := string(line)
		if m := trfTestCasePattern.FindStringSubmatch(line); m != nil {
			tc := createTRFTestCase(m[2], m[1])
			testCases[tc.DisplayName] = tc
			continue
		}
		// We make the assumption that the stderr message always follows a match to trfTestCaseStderr.
		// And we only capture the first stderr line after a match to trfTestCaseStderr is found.
		if m := trfTestCaseStderr.FindStringSubmatch(line); m != nil {
			currentTestName = m[1]
			if _, ok := errorMessages[currentTestName]; !ok {
				errorMessages[currentTestName] = &strings.Builder{}
				foundStderr = true
			}
			continue
		}
		if foundStderr {
			errorMessages[currentTestName].WriteString(line)
			foundStderr = false
			currentTestName = ""
			continue
		}
	}

	for testName, testCase := range testCases {
		if msg, ok := errorMessages[testName]; ok {
			if testCase.Status == Fail {
				testCase.FailReason = msg.String()
			}
		}
		res = append(res, testCase)
	}
	return res
}

func createTRFTestCase(caseName, result string) TestCaseResult {
	var status TestCaseStatus
	switch result {
	case "PASSED":
		status = Pass
	case "FAILED":
		status = Fail
	case "INCONCLUSIVE":
		status = Fail
	case "TIMED_OUT":
		status = Abort
	case "ERROR":
		status = Fail
	case "SKIPPED":
		status = Skip
	}
	return TestCaseResult{
		DisplayName: caseName,
		CaseName:    caseName,
		Status:      status,
		Format:      "FTF",
	}
}
