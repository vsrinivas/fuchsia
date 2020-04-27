// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is rustverned by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"regexp"
)

var (
	rustTestPreamblePattern = regexp.MustCompile(`running \d* tests?`)
	rustTestPassCasePattern = regexp.MustCompile(`test (.*?) ... ok`)
	rustTestFailCasePattern = regexp.MustCompile(`test (.*?) ... FAILED`)
	rustTestSkipCasePattern = regexp.MustCompile(`test (.*?) ... ignored`)
)

func parseRustTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	for _, line := range lines {
		var m [][]byte
		if m = rustTestPassCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Pass, nil, "Rust"))
			continue
		}
		if m = rustTestFailCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Fail, nil, "Rust"))
			continue
		}
		if m = rustTestSkipCasePattern.FindSubmatch(line); m != nil {
			res = append(res, makeTestCaseResult(m[1], Skip, nil, "Rust"))
			continue
		}
	}
	return res
}
