// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"encoding/json"
	"fmt"
	"regexp"
	"time"
)

var dartSystemTestPreamblePattern = regexp.MustCompile(`^\[----------\] Test results JSON:$`)

type dartSystemTestResults struct {
	TestGroups []TestGroup
}

type TestGroup struct {
	Name      string
	TestCases []TestCase
}

type TestCase struct {
	Name              string
	Result            string
	DurationInSeconds int
}

func parseDartSystemTest(lines [][]byte) []TestCaseResult {
	var jsonBytes []byte
	for _, line := range lines[1:] {
		jsonBytes = append(jsonBytes, line...)
	}
	parsed := dartSystemTestResults{}
	if err := json.Unmarshal(jsonBytes, &parsed); err != nil {
		return []TestCaseResult{}
	}

	var res []TestCaseResult
	for _, testGroup := range parsed.TestGroups {
		for _, testCase := range testGroup.TestCases {
			var status TestCaseStatus
			switch testCase.Result {
			case "PASSED":
				status = Pass
			case "FAILED":
				status = Fail
			}
			res = append(res, TestCaseResult{
				DisplayName: fmt.Sprintf("%s.%s", testGroup.Name, testCase.Name),
				SuiteName:   testGroup.Name,
				CaseName:    testCase.Name,
				Status:      status,
				Duration:    time.Duration(testCase.DurationInSeconds) * time.Second,
				Format:      "dart_system_test",
			})
		}
	}
	return res
}
