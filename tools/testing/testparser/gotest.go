// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"regexp"
)

var (
	goTestPreamblePattern = regexp.MustCompile(`==================== Test output for \S*:`)
	goTestPassCasePattern = regexp.MustCompile(`--- PASS: (\S*) \(([^\)]*)\)`)
	goTestFailCasePattern = regexp.MustCompile(`--- FAIL: (\S*) \(([^\)]*)\)`)
	goTestSkipCasePattern = regexp.MustCompile(`--- SKIP: (\S*) \(([^\)]*)\)`)
)

func parseGoTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	for _, line := range lines {
		var m [][]byte
		if m = goTestPassCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Pass, m[2], "Go"))
			continue
		}
		if m = goTestFailCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Fail, m[2], "Go"))
			continue
		}
		if m = goTestSkipCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Skip, m[2], "Go"))
			continue
		}
	}
	return res
}
