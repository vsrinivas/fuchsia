// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"
	"path/filepath"
	"testing"

	sinkpb "go.chromium.org/luci/resultdb/sink/proto/v1"
)

var testDataDir = flag.String("test_data_dir", "testdata", "Path to testdata/; only used in GN build")

func TestGetLUCICtx(t *testing.T) {
	old := os.Getenv("LUCI_CONTEXT")
	defer os.Setenv("LUCI_CONTEXT", old)
	os.Setenv("LUCI_CONTEXT", filepath.Join(*testDataDir, "lucictx.json"))
	ctx, err := resultSinkCtx()
	if err != nil {
		t.Errorf("Cannot parse LUCI_CONTEXT: %v", err)
	}
	if ctx.ResultSinkAddr != "result.sink" {
		t.Errorf("Incorrect value parsed for result_sink address. Got %s", ctx.ResultSinkAddr)
	}
	if ctx.AuthToken != "token" {
		t.Errorf("Incorrect value parsed for result_sink auth_token field. Got %s", ctx.AuthToken)
	}
}

func TestParse2Summary(t *testing.T) {
	t.Parallel()
	const chunkSize = 5
	var requests []*sinkpb.ReportTestResultsRequest
	expectRequests := 0
	for _, name := range []string{"summary.json", "summary2.json"} {
		summary, err := ParseSummary(filepath.Join(*testDataDir, name))
		if err != nil {
			log.Fatal(err)
		}
		testResults := SummaryToResultSink(summary, name)
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
