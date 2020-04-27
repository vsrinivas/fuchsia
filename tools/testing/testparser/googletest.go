// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"regexp"
)

var (
	googleTestPreamblePattern = regexp.MustCompile(`\[==========\] Running \d* tests? from \d* test (?:(?:suites?)|(?:cases?))\.`)
	googleTestPassCasePattern = regexp.MustCompile(`\[       OK \] (\S*) \(([^\)]*)\)`)
	googleTestFailCasePattern = regexp.MustCompile(`\[  FAILED  \] (\S*) \(([^\)]*)\)`)
	googleTestSkipCasePattern = regexp.MustCompile(`\[  SKIPPED \] (\S*) \(([^\)]*)\)`)
)

func parseGoogleTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	for _, line := range lines {
		var m [][]byte
		if m = googleTestPassCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Pass, m[2], "GoogleTest"))
			continue
		}
		if m = googleTestFailCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Fail, m[2], "GoogleTest"))
			continue
		}
		if m = googleTestSkipCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Skip, m[2], "GoogleTest"))
			continue
		}
	}
	return res
}
