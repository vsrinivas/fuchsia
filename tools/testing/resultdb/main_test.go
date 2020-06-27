// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"
	"path"
	"path/filepath"
	"testing"

	resultpb "go.chromium.org/luci/resultdb/proto/v1"
	sinkpb "go.chromium.org/luci/resultdb/sink/proto/v1"

	"go.fuchsia.dev/fuchsia/tools/lib/flagmisc"
)

var testDataFlag = flag.String("test_data_dir", "", "Path to testdata/")

func TestMain(m *testing.M) {
	flag.Parse()
	os.Exit(m.Run())
}

func TestGetLUCICtx(t *testing.T) {
	testDataDir := path.Join(filepath.Dir(os.Args[0]), *testDataFlag)
	luciCtx := path.Join(testDataDir, "lucictx.json")
	os.Setenv("LUCI_CONTEXT", luciCtx)
	ctx, err := resultSinkCtx()
	if err != nil {
		t.Errorf("Cannot parse LUCI_CONTEXT.")
	}
	if ctx.ResultSinkAddr != "result.sink" {
		t.Errorf("Incorrect value parsed for result_sink address. Got %s", ctx.ResultSinkAddr)
	}

	if ctx.AuthToken != "token" {
		t.Errorf("Incorrect value parsed for result_sink auth_token field. Got %s", ctx.AuthToken)
	}
}

func TestParse2Summary(t *testing.T) {
	testDataDir := path.Join(filepath.Dir(os.Args[0]), *testDataFlag)
	summaries = flagmisc.StringsValue{}
	summaries.Set(path.Join(testDataDir, "summary.json"))
	summaries.Set(path.Join(testDataDir, "summary2.json"))
	var requests []*sinkpb.ReportTestResultsRequest
	var expectRequests = 0
	var chunkSize = 5
	for _, summaryFile := range summaries {
		summary, err := ParseSummary(summaryFile)

		if err != nil {
			log.Fatal(err)
		}
		testResults := SummaryToResultSink(summary, []*resultpb.StringPair{
			{Key: "builder", Value: "fuchsia.x64"},
			{Key: "bucket", Value: "ci"},
		})
		expectRequests += (len(testResults)-1)/chunkSize + 1
		requests = append(requests, createTestResultsRequests(testResults, chunkSize)...)
		for _, testResult := range testResults {
			if len(testResult.TestId) == 0 {
				t.Errorf("Empty testId is not allowed.")
			}
		}
	}
	if len(requests) != expectRequests {
		t.Errorf("Incorrect number of request chuncks, got: %d want %d", len(requests), expectRequests)
	}
}

func TestParse2SummaryNoTags(t *testing.T) {
	testDataDir := path.Join(filepath.Dir(os.Args[0]), *testDataFlag)
	summaries = flagmisc.StringsValue{}
	summaries.Set(path.Join(testDataDir, "summary.json"))
	summaries.Set(path.Join(testDataDir, "summary2.json"))
	var requests []*sinkpb.ReportTestResultsRequest
	var expectRequests = 0
	var chunkSize = 5
	for _, summaryFile := range summaries {
		summary, err := ParseSummary(summaryFile)

		if err != nil {
			log.Fatal(err)
		}
		testResults := SummaryToResultSink(summary, []*resultpb.StringPair{})
		for _, r := range testResults {
			for _, tag := range r.Tags {
				if tag.Key == bucketTagKey || tag.Key == builderTagKey {
					t.Errorf("Unexpected tag key: %s, value: %s", tag.Key, tag.Value)
				}
			}
		}
		expectRequests += (len(testResults)-1)/chunkSize + 1
		requests = append(requests, createTestResultsRequests(testResults, chunkSize)...)
	}
	if len(requests) != expectRequests {
		t.Errorf("Incorrect number of request chuncks, got: %d, want %d", len(requests), expectRequests)
	}
}
