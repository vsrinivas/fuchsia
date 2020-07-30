// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"testing"
	"time"

	resultpb "go.chromium.org/luci/resultdb/proto/v1"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

func TestParseSummary(t *testing.T) {
	const testCount = 10
	summary := createTestSummary(testCount)
	testResults := SummaryToResultSink(summary, []*resultpb.StringPair{
		{Key: "builder", Value: "fuchsia.x64"},
		{Key: "bucket", Value: "ci"},
	})
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
