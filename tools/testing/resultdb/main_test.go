// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	resultpb "go.chromium.org/luci/resultdb/proto/v1"
	sinkpb "go.chromium.org/luci/resultdb/sink/proto/v1"
	"google.golang.org/protobuf/testing/protocmp"
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
		testResults := SummaryToResultSink(summary, []*resultpb.StringPair{}, name)
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

func TestStringPairConvert(t *testing.T) {
	tests := []struct {
		tag  string
		want *resultpb.StringPair
	}{
		{
			tag:  "swarming_id   :   foo_bar",
			want: &resultpb.StringPair{Key: "swarming_id", Value: "foo_bar"},
		},
		{
			tag:  "swarming-bot-id: abc-def",
			want: &resultpb.StringPair{Key: "swarming-bot-id", Value: "abc-def"},
		},
	}
	for _, tc := range tests {
		got, err := stringPairFromString(tc.tag)
		if err != nil {
			t.Errorf("stringPairFromString(%s) errored %v", tc.tag, err)
		}
		if diff := cmp.Diff(tc.want, got, protocmp.Transform()); diff != "" {
			t.Errorf("stringPairFromString diff (-want +got):\n%s", diff)
		}
	}
}

func TestConvertTags(t *testing.T) {
	tags := []string{"tag1:value1", "  tag2  :  value2  ", "tag3:"}
	out, err := convertTags(tags)
	if err != nil {
		t.Errorf("convertTags(%v) errored %v", tags, err)
	}
	if len(out) != len(tags) {
		t.Errorf("convertTags(%v) did not convert to the correct number of keypairs %v", tags, len(tags))
	}
	for _, pair := range out {
		if strings.Contains(pair.Key, " ") || strings.Contains(pair.Value, " ") {
			t.Errorf("convertTags(%v) = %v did not did not remove extra space", tags, pair)
		}
	}
}
