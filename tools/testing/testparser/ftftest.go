// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"regexp"
)

var (
	ftfTestPreamblePattern = regexp.MustCompile(`^Running test 'fuchsia-pkg:\/\/.*$`)
	ftfTestCasePattern     = regexp.MustCompile(`^\[(PASSED|FAILED|INCONCLUSIVE|TIMED OUT|ERROR|SKIPPED)\]\t(.*)$`)
)

func parseFtfTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	for _, line := range lines {
		line := string(line)
		m := ftfTestCasePattern.FindStringSubmatch(line)
		if m == nil {
			continue
		}
		var status TestCaseStatus
		switch m[1] {
		case "PASSED":
			status = Pass
		case "FAILED":
			status = Fail
		case "INCONCLUSIVE":
			status = Fail
		case "TIMED OUT":
			status = Fail
		case "ERROR":
			status = Fail
		case "SKIPPED":
			status = Skip
		}
		caseName := m[2]
		displayName := caseName
		res = append(res, TestCaseResult{
			DisplayName: displayName,
			CaseName:    caseName,
			Status:      status,
			Format:      "FTF",
		})
	}
	return res
}
