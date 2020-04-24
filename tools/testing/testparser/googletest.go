// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"regexp"
)

var (
	googleTestPreamblePattern = regexp.MustCompile("\\[==========\\] Running \\d* tests from \\d* test suite(s)?.")
	googleTestPassCasePattern = regexp.MustCompile("\\[       OK \\] (\\S*) \\(([\\w\\s\\.]*)\\)")
	googleTestFailCasePattern = regexp.MustCompile("\\[  FAILED  \\] (\\S*) \\(([\\w\\s\\.]*)\\)")
	googleTestSkipCasePattern = regexp.MustCompile("\\[  SKIPPED \\] (\\S*) \\(([\\w\\s\\.]*)\\)")
)

func parseGoogleTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	for _, line := range lines {
		var m [][]byte
		if m = googleTestPassCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Pass, m[2]))
			continue
		}
		if m = googleTestFailCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Fail, m[2]))
			continue
		}
		if m = googleTestSkipCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Skip, m[2]))
			continue
		}
	}
	return res
}
