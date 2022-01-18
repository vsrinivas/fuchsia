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
	zirconUtestPreamblePattern = regexp.MustCompile(`^CASE\s*(.*?)\s*\[STARTED\]$`)
	zirconUtestPassCasePattern = regexp.MustCompile(`^    (.*?)\s{1,51}\[RUNNING\] \[PASSED\] \((\d+) ms\)$`)
	zirconUtestFailCasePattern = regexp.MustCompile(`^    (.*?)\s{1,51}\[RUNNING\] \[FAILED\] \((\d+) ms\)$`)
	zirconUtestSkipCasePattern = regexp.MustCompile(`^    (.*?)\s{1,51}\[IGNORED\]$`)
)

func parseZirconUtest(lines [][]byte) []runtests.TestCaseResult {
	var res []runtests.TestCaseResult
	var suiteName string
	for _, line := range lines {
		line := string(line)
		var m []string
		if m = zirconUtestPreamblePattern.FindStringSubmatch(line); m != nil {
			suiteName = m[1]
		} else if m = zirconUtestPassCasePattern.FindStringSubmatch(line); m != nil {
			caseName := m[1]
			displayName := fmt.Sprintf("%s.%s", suiteName, caseName)
			duration, _ := time.ParseDuration(m[2] + "ms")
			res = append(res, runtests.TestCaseResult{
				DisplayName: displayName,
				SuiteName:   suiteName,
				CaseName:    caseName,
				Status:      runtests.TestSuccess,
				Duration:    duration,
				Format:      "Zircon utest",
			})
		} else if m = zirconUtestFailCasePattern.FindStringSubmatch(line); m != nil {
			caseName := m[1]
			displayName := fmt.Sprintf("%s.%s", suiteName, caseName)
			duration, _ := time.ParseDuration(m[2] + "ms")
			res = append(res, runtests.TestCaseResult{
				DisplayName: displayName,
				SuiteName:   suiteName,
				CaseName:    caseName,
				Status:      runtests.TestFailure,
				Duration:    duration,
				Format:      "Zircon utest",
			})
		} else if m = zirconUtestSkipCasePattern.FindStringSubmatch(line); m != nil {
			caseName := m[1]
			displayName := fmt.Sprintf("%s.%s", suiteName, caseName)
			res = append(res, runtests.TestCaseResult{
				DisplayName: displayName,
				SuiteName:   suiteName,
				CaseName:    caseName,
				Status:      runtests.TestSkipped,
				Format:      "Zircon utest",
			})
		}
	}
	return res
}
