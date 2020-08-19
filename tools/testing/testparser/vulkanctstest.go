// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"regexp"
	"strings"
)

var (
	ctsTestPreamblePattern  = regexp.MustCompile(`^dEQP Core .* starting\.\.$`)
	ctsTestCaseStartPattern = regexp.MustCompile(`^Test case '(.*)'\.\.$`)
	ctsTestCasePattern      = regexp.MustCompile(`^(Pass|Fail|QualityWarning|CompatibilityWarning|Pending|NotSupported|ResourceError|InternalError|Crash|Timeout) \(.*\)$`)
)

func parseVulkanCtsTest(lines [][]byte) []TestCaseResult {
	var res []TestCaseResult
	var displayName = ""
	var suiteName string
	var caseName string
	for _, line := range lines {
		line := string(line)
		m := ctsTestCaseStartPattern.FindStringSubmatch(line)
		if m != nil {
			displayName = m[1]
			dotIndex := strings.LastIndex(displayName, ".")
			suiteName = displayName[0:dotIndex]
			caseName = displayName[dotIndex+1:]
			continue
		}
		m = ctsTestCasePattern.FindStringSubmatch(line)
		if m == nil {
			continue
		}
		var status TestCaseStatus
		// List of results is in framework/qphelper/qpTestLog.c
		switch m[1] {
		case "Pass", "QualityWarning", "CompatibilityWarning":
			status = Pass
		case "Fail",
			"Pending",
			"ResourceError",
			"InternalError",
			"Crash",
			"Timeout":
			status = Fail
		case "NotSupported":
			status = Skip
		}
		res = append(res, TestCaseResult{
			DisplayName: displayName,
			SuiteName:   suiteName,
			CaseName:    caseName,
			Status:      status,
			Format:      "VulkanCtsTest",
		})
		displayName = ""
	}
	// Check for incomplete tests
	if displayName != "" {
		res = append(res, TestCaseResult{
			DisplayName: displayName,
			SuiteName:   suiteName,
			CaseName:    caseName,
			Status:      Fail,
			Format:      "VulkanCtsTest",
		})

	}
	return res
}
