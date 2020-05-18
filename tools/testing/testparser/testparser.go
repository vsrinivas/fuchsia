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
	res := []*regexp.Regexp{
		dartSystemTestPreamblePattern,
		ftfTestPreamblePattern,
		googleTestPreamblePattern,
		goTestPreamblePattern,
		rustTestPreamblePattern,
		zirconUtestPreamblePattern,
	}
	remaining_lines, match := firstMatch(lines, res)
	switch match {
	case dartSystemTestPreamblePattern:
		return parseDartSystemTest(remaining_lines)
	case ftfTestPreamblePattern:
		return parseFtfTest(lines)
	case googleTestPreamblePattern:
		return parseGoogleTest(remaining_lines)
	case goTestPreamblePattern:
		return parseGoTest(remaining_lines)
	case rustTestPreamblePattern:
		return parseRustTest(remaining_lines)
	case zirconUtestPreamblePattern:
		return parseZirconUtest(remaining_lines)
	default:
		return []TestCaseResult{}
	}
}

func firstMatch(lines [][]byte, res []*regexp.Regexp) ([][]byte, *regexp.Regexp) {
	for num, line := range lines {
		for _, re := range res {
			if re.Match(line) {
				return lines[num:], re
			}
		}
	}
	return nil, nil
}
