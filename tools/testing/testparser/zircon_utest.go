// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"fmt"
	"regexp"
	"strings"
	"time"
)

var (
	zirconUtestPreamblePattern = regexp.MustCompile(`CASE\s*(.*?)\s*\[STARTED\]`)
	zirconUtestPassCasePattern = regexp.MustCompile(`\s*(.*?)\s+\[RUNNING\] \[PASSED\] \((.*?)\)`)
	zirconUtestFailCasePattern = regexp.MustCompile(`\s*(.*?)\s+\[RUNNING\] \[FAILED\] \((.*?)\)`)
	zirconUtestSkipCasePattern = regexp.MustCompile(`\s*(.*?)\s+\[IGNORED\]`)
)

func parseZirconUtest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	var suiteName string
	for _, line := range lines {
		line := string(line)
		var m []string
		if m = zirconUtestPreamblePattern.FindStringSubmatch(line); m != nil {
			suiteName = m[1]
		} else if m = zirconUtestPassCasePattern.FindStringSubmatch(line); m != nil {
			caseName := m[1]
			displayName := fmt.Sprintf("%s.%s", suiteName, caseName)
			// Convert e.g. "4 ms" to "4ms" which parses to Duration successfully
			duration, _ := time.ParseDuration(strings.ReplaceAll(m[2], " ", ""))
			res = append(res, TestCaseResult{
				DisplayName: displayName,
				SuiteName:   suiteName,
				CaseName:    caseName,
				Status:      Pass,
				Duration:    duration,
				Format:      "Zircon utest",
			})
		} else if m = zirconUtestFailCasePattern.FindStringSubmatch(line); m != nil {
			caseName := m[1]
			displayName := fmt.Sprintf("%s.%s", suiteName, caseName)
			// Convert e.g. "4 ms" to "4ms" which parses to Duration successfully
			duration, _ := time.ParseDuration(strings.ReplaceAll(m[2], " ", ""))
			res = append(res, TestCaseResult{
				DisplayName: displayName,
				SuiteName:   suiteName,
				CaseName:    caseName,
				Status:      Fail,
				Duration:    duration,
				Format:      "Zircon utest",
			})
		} else if m = zirconUtestSkipCasePattern.FindStringSubmatch(line); m != nil {
			caseName := m[1]
			displayName := fmt.Sprintf("%s.%s", suiteName, caseName)
			res = append(res, TestCaseResult{
				DisplayName: displayName,
				SuiteName:   suiteName,
				CaseName:    caseName,
				Status:      Skip,
				Format:      "Zircon utest",
			})
		}
	}
	return res
}
