// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package testparser parses test stdout into structured results.
package testparser

import (
	"bytes"
	"regexp"
)

// Parse takes stdout from a test program and returns structured results.
// Internally, a variety of test program stdout formats are supported.
// If no structured results were identified, an empty slice is returned.
func Parse(stdout []byte) []TestCaseResult {
	lines := bytes.Split(stdout, []byte{'\n'})
	res := []*regexp.Regexp{googleTestPreamblePattern, goTestPreamblePattern}
	match := firstMatch(lines, res)
	switch match {
	case googleTestPreamblePattern:
		return parseGoogleTest(lines)
	case goTestPreamblePattern:
		return parseGoTest(lines)
	// TODO(shayba): add support for more test frameworks
	default:
		return []TestCaseResult{}
	}
}

func firstMatch(lines [][]byte, res []*regexp.Regexp) *regexp.Regexp {
	for _, line := range lines {
		for _, re := range res {
			if re.Match(line) {
				return re
			}
		}
	}
	return nil
}
