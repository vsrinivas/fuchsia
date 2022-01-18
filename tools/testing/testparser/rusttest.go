// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is rustverned by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"fmt"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

var (
	rustTestPreamblePattern = regexp.MustCompile(`^running \d* tests?$`)
	// ex: "test channel::tests::channel_call_etc_timeout ... ok"
	//   test_suite = "channel"
	//   test_case  = "tests:channel_call_etc_timeout"
	//   status     = "ok"
	// ex: "test version::tests::get_version_string ... FAILED"
	//   test_suite = "version"
	//   test_case  = "tests:get_version_string"
	//   status     = "FAILED"
	rustTestCasePattern = regexp.MustCompile(`^test (?:(?P<test_suite>.*?)::)?(?P<test_case>.*?) \.\.\. (?P<status>\w*)$`)
	// ex: "---- version::tests::get_version_string stderr ----"
	rustTestCaseStderrStart = regexp.MustCompile(`^-{4} (?P<test_name>.*?) stderr -{4}$`)
	// If we see any of these keyword(s) in stdout line, we either want to stop capturing for
	// test stderr, or think it's the last of the stderr section.
	// "stack backtrace" - we don't want to capture the stacktrace it would make fail clustering hard
	// "failures:" - this seem to be the last section in rusttest that reports a list of test fails
	rustTestCaseStderrEnd = regexp.MustCompile(`^(stack backtrace|failures):$`)
)

func parseRustTest(lines [][]byte) []runtests.TestCaseResult {
	var res []runtests.TestCaseResult
	testCases := make(map[string]runtests.TestCaseResult)
	errorMessages := make(map[string]*strings.Builder)
	currentTestName := ""
	for _, line := range lines {
		line := string(line)
		// This is just spam, we should stop printing this line.
		// TODO(yuanzhi) Remove once we've stopped logging this line.
		if strings.HasPrefix(line, "[stdout - legacy_test]") {
			continue
		}
		if m := rustTestCasePattern.FindStringSubmatch(line); m != nil {
			tc := createRustTestCase(m[1], m[2], m[3])
			testCases[tc.DisplayName] = tc
			continue
		}
		// Note: we should prioritize matching the start of the stderr than the end.
		// Because matching the end is more fragile than the start.
		if m := rustTestCaseStderrStart.FindStringSubmatch(line); m != nil {
			currentTestName = m[1]
			errorMessages[currentTestName] = &strings.Builder{}
			continue
		}
		if m := rustTestCaseStderrEnd.MatchString(line); m {
			currentTestName = ""
			continue
		}
		if currentTestName != "" {
			errorMessages[currentTestName].WriteString(line + "\n")
			continue
		}
	}

	for testName, testCase := range testCases {
		if msg, ok := errorMessages[testName]; ok {
			if testCase.Status == runtests.TestFailure {
				testCase.FailReason = strings.TrimSuffix(msg.String(), "\n")
			}
		}
		res = append(res, testCase)
	}
	return res
}

func createRustTestCase(suiteName, caseName, result string) runtests.TestCaseResult {
	displayName := caseName
	if suiteName != "" {
		displayName = fmt.Sprintf("%s::%s", suiteName, caseName)
	}
	var status runtests.TestResult
	switch result {
	case "ok":
		status = runtests.TestSuccess
	case "FAILED":
		status = runtests.TestFailure
	case "ignored":
		status = runtests.TestSkipped
	}
	return runtests.TestCaseResult{
		DisplayName: displayName,
		SuiteName:   suiteName,
		CaseName:    caseName,
		Status:      status,
		Format:      "Rust",
	}
}
