// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"fmt"
	"regexp"
)

var (
	rustTestPreamblePattern = regexp.MustCompile(`^running \d* tests?$`)
	rustTestCasePattern     = regexp.MustCompile(`^test (?:(.*?)::)?(.*?) \.\.\. (\w*)$`)
)

func parseRustTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	for _, line := range lines {
		line := string(line)
		m := rustTestCasePattern.FindStringSubmatch(line)
		if m == nil {
			continue
		}
		suiteName := m[1]
		caseName := m[2]
		displayName := caseName
		if suiteName != "" {
			displayName = fmt.Sprintf("%s::%s", suiteName, caseName)
		}
		var status TestCaseStatus
		switch m[3] {
		case "ok":
			status = Pass
		case "FAILED":
			status = Fail
		case "ignored":
			status = Skip
		}
		res = append(res, TestCaseResult{
			DisplayName: displayName,
			SuiteName:   suiteName,
			CaseName:    caseName,
			Status:      status,
			Format:      "Rust",
		})
	}
	return res
}
