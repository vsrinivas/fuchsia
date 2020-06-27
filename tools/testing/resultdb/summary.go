// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"time"

	"github.com/golang/protobuf/ptypes"
	resultpb "go.chromium.org/luci/resultdb/proto/v1"
	sinkpb "go.chromium.org/luci/resultdb/sink/proto/v1"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	testparser "go.fuchsia.dev/fuchsia/tools/testing/testparser/lib"
)

// ParseSummary unmarshalls the summary.json file content into runtests.TestSummary struct.
func ParseSummary(filePath string) (*runtests.TestSummary, error) {
	content, err := ioutil.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	var summary runtests.TestSummary
	if err := json.Unmarshal(content, &summary); err != nil {
		return nil, err
	}
	return &summary, nil
}

// SummaryToResultSink converts runtests.TestSummary data into an array of result_sink TestResult.
func SummaryToResultSink(s *runtests.TestSummary, tags []*resultpb.StringPair) []*sinkpb.TestResult {
	r := []*sinkpb.TestResult{}
	for _, test := range s.Tests {
		if len(test.Cases) > 0 {
			testCases := testCaseToResultSink(test.Cases, &test, tags)
			r = append(r, testCases...)
		} else {
			if testResult, err := testDetailsToResultSink(&test, tags); err == nil {
				r = append(r, testResult)
			}
		}
	}
	return r
}

// testCaseToResultSink converts TestCaseResult defined in //tools/testing/testparser/result.go
// to ResultSink's TestResult. A testcase will not be converted if test result cannot be
// mapped to result_sink.Status.
func testCaseToResultSink(testCases []testparser.TestCaseResult, testDetail *runtests.TestDetails, tags []*resultpb.StringPair) []*sinkpb.TestResult {
	testResult := []*sinkpb.TestResult{}
	resultTags := make([]*resultpb.StringPair, len(tags))
	copy(resultTags, tags)

	for _, testCase := range testCases {
		testID := fmt.Sprintf("%s/%s:%s", testDetail.Name, testCase.SuiteName, testCase.CaseName)
		r := sinkpb.TestResult{
			TestId: testID,
			Tags:   append(resultTags, &resultpb.StringPair{Key: "format", Value: testCase.Format}),
		}
		status, err := testCaseStatusToResultDBStatus(testCase.Status)
		if err != nil {
			log.Printf("[Warn] Skip uploading testcase: %s to ResultDB due to error: %v", testID, err)
			continue
		}
		r.Status = status
		if startTime, err := ptypes.TimestampProto(testDetail.StartTime); err == nil {
			r.StartTime = startTime
		}
		if testCase.Duration > 0 {
			r.Duration = ptypes.DurationProto(testCase.Duration)
		}
		r.Expected = determineExpected(r.Status)
		testResult = append(testResult, &r)
	}
	return testResult
}

// testCaseToResultSink converts TestDetail defined in /tools/testing/runtests/runtests.go
// to ResultSink's TestResult. Returns (nil, error) is a test result cannot be mapped to
// result_sink.Status
func testDetailsToResultSink(testDetail *runtests.TestDetails, tags []*resultpb.StringPair) (*sinkpb.TestResult, error) {
	r := sinkpb.TestResult{
		TestId: testDetail.Name,
		Tags:   tags,
	}
	status, err := testDetailResultToResultDBStatus(testDetail.Result)
	if err != nil {
		log.Printf("[Warn] Skip uploading testcase: %s to ResultDB due to error: %v", testDetail.Name, err)
		return nil, err
	}
	r.Status = status

	if startTime, err := ptypes.TimestampProto(testDetail.StartTime); err == nil {
		r.StartTime = startTime
	}
	if testDetail.DurationMillis > 0 {
		r.Duration = ptypes.DurationProto(time.Duration(testDetail.DurationMillis) * time.Millisecond)
	}
	r.Expected = determineExpected(r.Status)
	return &r, nil
}

func determineExpected(status sinkpb.TestStatus) bool {
	switch status {
	case sinkpb.TestStatus_PASS:
		return true
	case sinkpb.TestStatus_SKIP:
		return true
	case sinkpb.TestStatus_FAIL:
		return false
	case sinkpb.TestStatus_CRASH:
		return false
	case sinkpb.TestStatus_ABORT:
		return false
	}
	return false
}

func testCaseStatusToResultDBStatus(result testparser.TestCaseStatus) (sinkpb.TestStatus, error) {
	switch result {
	case testparser.Pass:
		return sinkpb.TestStatus_PASS, nil
	case testparser.Fail:
		return sinkpb.TestStatus_FAIL, nil
	case testparser.Skip:
		return sinkpb.TestStatus_SKIP, nil
	}
	return sinkpb.TestStatus_STATUS_UNSPECIFIED, fmt.Errorf("Cannot map Result: %s to result_sink test_result status", result)
}

func testDetailResultToResultDBStatus(result runtests.TestResult) (sinkpb.TestStatus, error) {
	switch result {
	case runtests.TestSuccess:
		return sinkpb.TestStatus_PASS, nil
	case runtests.TestFailure:
		return sinkpb.TestStatus_FAIL, nil
	}
	return sinkpb.TestStatus_STATUS_UNSPECIFIED, fmt.Errorf("Cannot map Result: %s to result_sink test_result status", result)
}
