// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"fmt"
	"regexp"
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

var (
	googleTestPreamblePattern = regexp.MustCompile(`^\[==========\] Running \d* tests? from \d* test (?:(?:suites?)|(?:cases?))\.$`)
	googleTestCasePattern     = regexp.MustCompile(`^\[(       OK |  FAILED  |  SKIPPED )\] (.*?)\.(.*?) \((\d+) ms\)$`)
)

func parseGoogleTest(lines [][]byte) []runtests.TestCaseResult {
	var res []runtests.TestCaseResult
	for _, line := range lines {
		line := string(line)
		m := googleTestCasePattern.FindStringSubmatch(line)
		if m == nil {
			continue
		}
		var status runtests.TestResult
		switch m[1] {
		case "       OK ":
			status = runtests.TestSuccess
		case "  FAILED  ":
			status = runtests.TestFailure
		case "  SKIPPED ":
			status = runtests.TestSkipped
		}
		suiteName := m[2]
		caseName := m[3]
		displayName := fmt.Sprintf("%s.%s", suiteName, caseName)
		duration, _ := time.ParseDuration(m[4] + "ms")
		res = append(res, runtests.TestCaseResult{
			DisplayName: displayName,
			SuiteName:   suiteName,
			CaseName:    caseName,
			Status:      status,
			Duration:    duration,
			Format:      "GoogleTest",
		})
	}
	return res
}
