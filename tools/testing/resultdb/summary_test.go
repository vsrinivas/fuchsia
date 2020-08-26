// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"path/filepath"
	"testing"
	"time"

	sinkpb "go.chromium.org/luci/resultdb/sink/proto/v1"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

func TestParseSummary(t *testing.T) {
	const testCount = 10
	summary := createTestSummary(testCount)
	testResults := SummaryToResultSink(summary, "")
	if len(testResults) != testCount {
		t.Errorf(
			"Parsed incorrect number of resultdb tests in TestSummary, got %d, want %d",
			len(testResults), testCount)
	}
	requests := createTestResultsRequests(testResults, testCount)
	if len(requests) != 1 {
		t.Errorf(
			"Grouped incorrect chunks of ResultDB sink requests, got %d, want 1",
			len(requests))
	}
	if len(requests[0].TestResults) != testCount {
		t.Errorf(
			"Incorrect number of TestResult in the first chunk, got %d, want %d",
			len(requests[0].TestResults), testCount)

	}
	if requests[0].TestResults[0].TestId != "test_0" {
		t.Errorf("Incorrect TestId parsed for first suite. got %s, want test_0", requests[0].TestResults[0].TestId)
	}
}

func createTestSummary(testCount int) *runtests.TestSummary {
	t := []runtests.TestDetails{}
	for i := 0; i < testCount; i++ {
		t = append(t, runtests.TestDetails{
			Name:                 fmt.Sprintf("test_%d", i),
			GNLabel:              "some label",
			OutputFile:           "some file path",
			Result:               runtests.TestSuccess,
			StartTime:            time.Now(),
			DurationMillis:       39797,
			IsTestingFailureMode: false,
		})
	}
	return &runtests.TestSummary{Tests: t}
}

func TestIsReadable(t *testing.T) {
	if r := isReadable(""); r {
		t.Errorf("Empty string cannot be readable. got %t, want false", r)
	}
	if r := isReadable(*testDataFlag); r {
		t.Errorf("Directory should not be readable. got %t, want false", r)
	}
	luciCtx := filepath.Join(*testDataFlag, "lucictx.json")
	if r := isReadable(luciCtx); !r {
		t.Errorf("File %v should be readable. got %t, want true", luciCtx, r)
	}
}

func TestDetermineExpected(t *testing.T) {
	testCases := []struct {
		testStatus     sinkpb.TestStatus
		testCaseStatus sinkpb.TestStatus
		expected       bool
	}{
		{
			// test passed, test case result is ignored.
			testStatus:     sinkpb.TestStatus_PASS,
			testCaseStatus: sinkpb.TestStatus_FAIL,
			expected:       true,
		},
		{
			// test failed and has test case status,
			// report on test case result.
			testStatus:     sinkpb.TestStatus_FAIL,
			testCaseStatus: sinkpb.TestStatus_PASS,
			expected:       true,
		},
		{
			// test failed and no test case status,
			// report test result.
			testStatus:     sinkpb.TestStatus_FAIL,
			testCaseStatus: sinkpb.TestStatus_STATUS_UNSPECIFIED,
			expected:       false,
		},
		{
			// cannot determine test status,
			// report on test cast result.
			testStatus:     sinkpb.TestStatus_STATUS_UNSPECIFIED,
			testCaseStatus: sinkpb.TestStatus_PASS,
			expected:       true,
		},
		{
			// cannot determine both test and test case result
			testStatus:     sinkpb.TestStatus_STATUS_UNSPECIFIED,
			testCaseStatus: sinkpb.TestStatus_STATUS_UNSPECIFIED,
			expected:       false,
		},
		{
			testStatus:     sinkpb.TestStatus_PASS,
			testCaseStatus: sinkpb.TestStatus_PASS,
			expected:       true,
		},
		{
			testStatus:     sinkpb.TestStatus_FAIL,
			testCaseStatus: sinkpb.TestStatus_FAIL,
			expected:       false,
		},
	}
	for _, tc := range testCases {
		r := determineExpected(tc.testStatus, tc.testCaseStatus)
		if r != tc.expected {
			t.Errorf("TestDetermineExpected failed:\ntestSuite Status: %v, testCase Status: %v, got %t, want %t",
				tc.testStatus, tc.testCaseStatus, r, tc.expected)
		}
	}
}
