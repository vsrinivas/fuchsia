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
	remainingLines, match := firstMatch(lines, res)

	var cases []TestCaseResult
	switch match {
	case dartSystemTestPreamblePattern:
		cases = parseDartSystemTest(remainingLines)
	case ftfTestPreamblePattern:
		cases = parseFtfTest(lines)
	case googleTestPreamblePattern:
		cases = parseGoogleTest(remainingLines)
	case goTestPreamblePattern:
		cases = parseGoTest(remainingLines)
	case rustTestPreamblePattern:
		cases = parseRustTest(remainingLines)
	case zirconUtestPreamblePattern:
		cases = parseZirconUtest(remainingLines)
	}

	// Ensure that an empty set of cases is serialized to JSON as an empty
	// array, not as null.
	if cases == nil {
		cases = []TestCaseResult{}
	}
	return cases
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
