// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"fmt"
	"path/filepath"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/parseoutput"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

var networkConformanceParseFailureDisplayName = "Network Conformance result parse failure"

func parseNetworkConformanceTest(lines [][]byte) []runtests.TestCaseResult {
	var res []runtests.TestCaseResult
	for i, line := range lines {
		line := string(line)

		parsedCaseEnd, matched, err := parseoutput.ParseNetworkConformanceCaseEnd(
			line,
		)
		if !matched {
			continue
		}
		if err != nil {
			res = append(res, runtests.TestCaseResult{
				DisplayName: networkConformanceParseFailureDisplayName,
				CaseName:    networkConformanceParseFailureDisplayName,
				Status:      runtests.TestFailure,
				Format:      parseoutput.NetworkConformanceFormatName,
				FailReason: fmt.Sprintf(
					"error while parsing network-conformance case in line %d: %s",
					i,
					err,
				),
			})
			continue
		}

		result := runtests.TestCaseResult{
			DisplayName: parsedCaseEnd.Identifier.String(),
			SuiteName:   parsedCaseEnd.Identifier.SuiteName,
			CaseName: fmt.Sprintf(
				"%d.%d",
				parsedCaseEnd.Identifier.MajorNumber,
				parsedCaseEnd.Identifier.MinorNumber,
			),
			Duration: time.Millisecond * time.Duration(parsedCaseEnd.DurationMillis),
			Format:   parseoutput.NetworkConformanceFormatName,
		}

		if parsedCaseEnd.LogFile != "" {
			dir, f := filepath.Split(parsedCaseEnd.LogFile)
			result.OutputDir = dir
			result.OutputFiles = []string{f}
		}

		if parsedCaseEnd.ActualOutcome == parsedCaseEnd.ExpectedOutcome {
			result.Status = runtests.TestSuccess
		} else {
			result.Status = runtests.TestFailure
			result.FailReason = fmt.Sprintf(
				"actual test outcome %s does not match expected test outcome %s",
				parsedCaseEnd.ActualOutcome,
				parsedCaseEnd.ExpectedOutcome,
			)
		}

		res = append(res, result)
	}
	return res
}
