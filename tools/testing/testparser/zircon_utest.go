// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"regexp"
)

var (
	zirconUtestPreamblePattern = regexp.MustCompile(`CASE .{50} \[STARTED\]`)
	zirconUtestPassCasePattern = regexp.MustCompile(`    (.*?)\s+\[RUNNING\] \[PASSED\] \((.*?)\)`)
	zirconUtestFailCasePattern = regexp.MustCompile(`    (.*?)\s+\[RUNNING\] \[FAILED\] \((.*?)\)`)
	zirconUtestSkipCasePattern = regexp.MustCompile(`    (.*?)\s+\[IGNORED\]`)
)

func parseZirconUtest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	for _, line := range lines {
		var m [][]byte
		if m = zirconUtestPassCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Pass, m[2], "Zircon utest"))
			continue
		}
		if m = zirconUtestFailCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Fail, m[2], "Zircon utest"))
			continue
		}
		if m = zirconUtestSkipCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Skip, nil, "Zircon utest"))
			continue
		}
	}
	return res
}
