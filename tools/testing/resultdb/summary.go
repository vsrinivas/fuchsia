// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"time"

	resultpb "go.chromium.org/luci/resultdb/proto/v1"
	sinkpb "go.chromium.org/luci/resultdb/sink/proto/v1"
	"google.golang.org/protobuf/types/known/durationpb"
	"google.golang.org/protobuf/types/known/timestamppb"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

// Test name is limited to 512 bytes max.
// https://source.chromium.org/chromium/infra/infra/+/main:go/src/go.chromium.org/luci/resultdb/pbutil/test_result.go;l=44;drc=910bdba5763842c67a81741b7cb26e7f7d2793fc
const (
	MAX_TEST_ID_SIZE_BYTES     = 512
	MAX_FAIL_REASON_SIZE_BYTES = 1024
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
func SummaryToResultSink(s *runtests.TestSummary, tags []*resultpb.StringPair, outputRoot string) ([]*sinkpb.TestResult, []string) {
	if len(outputRoot) == 0 {
		outputRoot, _ = os.Getwd()
	}
	rootPath, _ := filepath.Abs(outputRoot)
	var r []*sinkpb.TestResult
	var ts []string
	for _, test := range s.Tests {
		if len(test.Cases) > 0 {
			testCases, testsSkipped := testCaseToResultSink(test.Cases, tags, &test, rootPath)
			r = append(r, testCases...)
			ts = append(ts, testsSkipped...)
		}
		if testResult, testsSkipped, err := testDetailsToResultSink(tags, &test, rootPath); err == nil {
			r = append(r, testResult)
			ts = append(ts, testsSkipped...)
		}
	}
	return r, ts
}

// invocationLevelArtifacts creates resultdb artifacts for syslog and serial log to be sent to ResultDB.
func invocationLevelArtifacts(outputRoot string) map[string]*sinkpb.Artifact {
	if len(outputRoot) == 0 {
		outputRoot, _ = os.Getwd()
	}
	rootPath, _ := filepath.Abs(outputRoot)
	artifacts := map[string]*sinkpb.Artifact{}

	// TODO(yuanzhi) Make this an argument that the recipe can pass to the resultdb uploader
	// instead of hardcoding here.
	for _, invocationLog := range [...]string{
		"infra_and_test_std_and_klog.txt",
		"serial_log.txt",
		"syslog.txt",
		"triage_output",
	} {
		logFile := filepath.Join(rootPath, invocationLog)
		if isReadable(logFile) {
			artifacts[invocationLog] = &sinkpb.Artifact{
				Body:        &sinkpb.Artifact_FilePath{FilePath: logFile},
				ContentType: "text/plain",
			}
		}
	}
	return artifacts
}

// testCaseToResultSink converts TestCaseResult defined in //tools/testing/testparser/result.go
// to ResultSink's TestResult. A testcase will not be converted if test result cannot be
// mapped to result_sink.Status.
func testCaseToResultSink(testCases []runtests.TestCaseResult, tags []*resultpb.StringPair, testDetail *runtests.TestDetails, outputRoot string) ([]*sinkpb.TestResult, []string) {
	var testResult []*sinkpb.TestResult
	var testsSkipped []string

	// Ignore error, testStatus will be set to resultpb.TestStatus_STATUS_UNSPECIFIED if error != nil.
	// And when passed to determineExpected, resultpb.TestStatus_STATUS_UNSPECIFIED will be handled correctly.
	testStatus, _ := resultDBStatus(testDetail.Result)

	for _, testCase := range testCases {
		testID := fmt.Sprintf("%s/%s:%s", testDetail.Name, testCase.SuiteName, testCase.CaseName)
		if len(testID) > MAX_TEST_ID_SIZE_BYTES {
			log.Printf("[ERROR] Skip uploading to ResultDB due to test_id exceeding %d bytes max limit: %q", MAX_TEST_ID_SIZE_BYTES, testID)
			testsSkipped = append(testsSkipped, testID)
			continue
		}
		r := sinkpb.TestResult{
			TestId: testID,
			Tags:   append([]*resultpb.StringPair{{Key: "format", Value: testCase.Format}}, tags...),
		}
		testCaseStatus, err := resultDBStatus(testCase.Status)
		if err != nil {
			log.Printf("[Warn] Skip uploading testcase: %s to ResultDB due to error: %v", testID, err)
			continue
		}
		if testCase.FailReason != "" {
			r.FailureReason = &resultpb.FailureReason{PrimaryErrorMessage: truncateString(testCase.FailReason, MAX_FAIL_REASON_SIZE_BYTES)}
		}
		r.Status = testCaseStatus
		r.StartTime = timestamppb.New(testDetail.StartTime)
		if testCase.Duration > 0 {
			r.Duration = durationpb.New(testCase.Duration)
		}
		r.Expected = determineExpected(testStatus, testCaseStatus)
		r.Artifacts = make(map[string]*sinkpb.Artifact)
		for _, of := range testCase.OutputFiles {
			outputFile := filepath.Join(outputRoot, of)
			if isReadable(outputFile) {
				r.Artifacts[filepath.Base(outputFile)] = &sinkpb.Artifact{
					Body: &sinkpb.Artifact_FilePath{FilePath: outputFile},
				}
			} else {
				log.Printf("[Warn] outputFile: %s is not readable, skip.", outputFile)
			}
		}

		testResult = append(testResult, &r)
	}
	return testResult, testsSkipped
}

// testDetailsToResultSink converts TestDetail defined in /tools/testing/runtests/runtests.go
// to ResultSink's TestResult. Returns (nil, error) if a test result cannot be mapped to
// result_sink.Status
func testDetailsToResultSink(tags []*resultpb.StringPair, testDetail *runtests.TestDetails, outputRoot string) (*sinkpb.TestResult, []string, error) {
	var testsSkipped []string
	if len(testDetail.Name) > MAX_TEST_ID_SIZE_BYTES {
		testsSkipped = append(testsSkipped, testDetail.Name)
		log.Printf("[ERROR] Skip uploading to ResultDB due to test_id exceeding %d bytes max limit: %q", MAX_TEST_ID_SIZE_BYTES, testDetail.Name)
		return nil, testsSkipped, fmt.Errorf("The test name exceeds %d bytes max limit: %q ", MAX_TEST_ID_SIZE_BYTES, testDetail.Name)
	}
	r := sinkpb.TestResult{
		TestId: testDetail.Name,
		Tags: append([]*resultpb.StringPair{
			{Key: "gn_label", Value: testDetail.GNLabel},
			{Key: "test_case_count", Value: strconv.Itoa(len(testDetail.Cases))},
			{Key: "affected", Value: strconv.FormatBool(testDetail.Affected)},
		}, tags...),
	}
	testStatus, err := resultDBStatus(testDetail.Result)
	if err != nil {
		log.Printf("[Warn] Skip uploading test target: %s to ResultDB due to error: %v", testDetail.Name, err)
		return nil, testsSkipped, err
	}
	r.Status = testStatus

	r.StartTime = timestamppb.New(testDetail.StartTime)
	if testDetail.DurationMillis > 0 {
		r.Duration = durationpb.New(time.Duration(testDetail.DurationMillis) * time.Millisecond)
	}
	r.Artifacts = make(map[string]*sinkpb.Artifact)
	for _, of := range testDetail.OutputFiles {
		outputFile := filepath.Join(outputRoot, of)
		if isReadable(outputFile) {
			r.Artifacts[filepath.Base(outputFile)] = &sinkpb.Artifact{
				Body: &sinkpb.Artifact_FilePath{FilePath: outputFile},
			}
		} else {
			log.Printf("[Warn] outputFile: %s is not readable, skip.", outputFile)
		}
	}

	r.SummaryHtml = `<details><summary>triage_output</summary>
	<pre><text-artifact artifact-id="triage_output" inv-level/></pre>
	</details>
	`

	r.Expected = determineExpected(testStatus, resultpb.TestStatus_STATUS_UNSPECIFIED)
	return &r, testsSkipped, nil
}

// determineExpected checks if a test result is expected.
//
// For example, if a test case failed but fail is the correct behavior, we will mark
// expected to true. On the other hand, if a test case failed and failure is the incorrect
// behavior then we will mark expected to false. This is completely determined by
// the status recorded by the test suite vs. status recorded for the test case.
//
// If a test is reported "PASS", then we will report all test cases within the same
// test to pass as well. If a test is reported other than "PASS" or "SKIP", we will
// process the test cases based on the test case result.
func determineExpected(testStatus resultpb.TestStatus, testCaseStatus resultpb.TestStatus) bool {
	switch testStatus {
	case resultpb.TestStatus_PASS, resultpb.TestStatus_SKIP:
		return true
	case resultpb.TestStatus_FAIL, resultpb.TestStatus_CRASH, resultpb.TestStatus_ABORT, resultpb.TestStatus_STATUS_UNSPECIFIED:
		switch testCaseStatus {
		case resultpb.TestStatus_PASS, resultpb.TestStatus_SKIP:
			return true
		case resultpb.TestStatus_FAIL, resultpb.TestStatus_CRASH, resultpb.TestStatus_ABORT, resultpb.TestStatus_STATUS_UNSPECIFIED:
			return false
		}
	}
	return false
}

func resultDBStatus(result runtests.TestResult) (resultpb.TestStatus, error) {
	switch result {
	case runtests.TestSuccess:
		return resultpb.TestStatus_PASS, nil
	case runtests.TestFailure:
		return resultpb.TestStatus_FAIL, nil
	case runtests.TestSkipped:
		return resultpb.TestStatus_SKIP, nil
	case runtests.TestAborted:
		return resultpb.TestStatus_ABORT, nil
	}
	return resultpb.TestStatus_STATUS_UNSPECIFIED, fmt.Errorf("cannot map Result: %s to result_sink test_result status", result)
}

func isReadable(p string) bool {
	if len(p) == 0 {
		return false
	}
	info, err := os.Stat(p)
	if err != nil {
		return false
	}
	if info.IsDir() {
		return false
	}
	f, err := os.Open(p)
	if err != nil {
		return false
	}
	_ = f.Close()
	return true
}

func truncateString(str string, maxLength int) string {
	if len(str) <= maxLength {
		return str
	}
	// We want to append "..." to maxLength, which takes up 3 spaces. If maxLength is less than that, just return empty.
	if maxLength <= 3 {
		return ""
	}
	runes := []rune(str)
	byteCount := 0
	for _, char := range runes {
		if byteCount+len(string(char)) > (maxLength - 3) {
			if byteCount == 0 {
				return ""
			}
			return str[:byteCount] + "..."
		}
		byteCount = byteCount + len(string(char))
	}
	return str
}
