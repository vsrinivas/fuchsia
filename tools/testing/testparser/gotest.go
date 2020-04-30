// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"fmt"
	"regexp"
	"time"
)

var (
	goTestPreamblePattern = regexp.MustCompile(`==================== Test output for (.*?):`)
	goTestCasePattern     = regexp.MustCompile(`--- (\w*?): (.*?) \((.*?)\)`)
)

func parseGoTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	var suiteName string
	for _, line := range lines {
		line := string(line)
		m := goTestPreamblePattern.FindStringSubmatch(line)
		if m != nil {
			suiteName = m[1]
			continue
		}
		m = goTestCasePattern.FindStringSubmatch(line)
		if m == nil {
			continue
		}
		var status TestCaseStatus
		switch m[1] {
		case "PASS":
			status = Pass
		case "FAIL":
			status = Fail
		case "SKIP":
			status = Skip
		}
		caseName := m[2]
		displayName := fmt.Sprintf("%s.%s", suiteName, caseName)
		duration, _ := time.ParseDuration(m[3])
		res = append(res, TestCaseResult{
			DisplayName: displayName,
			SuiteName:   suiteName,
			CaseName:    caseName,
			Status:      status,
			Duration:    duration,
			Format:      "Go",
		})
	}
	return res
}
