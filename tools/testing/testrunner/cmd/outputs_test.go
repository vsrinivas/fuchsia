// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner"
)

func TestRecordingOfOutputs(t *testing.T) {
	start := time.Unix(0, 0)
	results := []testrunner.TestResult{
		{
			Name:      "fuchsia-pkg://foo#test_a",
			GNLabel:   "//a/b/c:test_a(//toolchain)",
			Result:    runtests.TestFailure,
			StartTime: start,
			EndTime:   start.Add(5 * time.Millisecond),
			DataSinks: runtests.DataSinkReference{
				"sinks": []runtests.DataSink{
					{
						Name: "SINK_A1",
						File: "sink_a1.txt",
					},
					{
						Name: "SINK_A2",
						File: "sink_a2.txt",
					},
				},
			},
			Stdio: []byte("STDOUT_A"),
		},
		{
			Name:      "test_b",
			GNLabel:   "//a/b/c:test_b(//toolchain)",
			Result:    runtests.TestSuccess,
			StartTime: start,
			EndTime:   start.Add(10 * time.Millisecond),
			DataSinks: runtests.DataSinkReference{
				"sinks": []runtests.DataSink{
					{
						Name: "SINK_B",
						File: "sink_b.txt",
					},
				},
			},
			Stdio: []byte("STDERR_B"),
		},
	}

	dataDir, err := ioutil.TempDir("", "testrunner_tests")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(dataDir)
	outDir := filepath.Join(dataDir, "out")

	var buf bytes.Buffer
	producer := tap.NewProducer(&buf)
	producer.Plan(len(results))
	o, err := createTestOutputs(producer, outDir)
	if err != nil {
		t.Fatalf("failed to create a test outputs object: %v", err)
	}
	defer o.Close()

	outputFileA := filepath.Join("fuchsia-pkg/foo/test_a", "0", "stdout-and-stderr.txt")
	outputFileB := filepath.Join("test_b", "0", "stdout-and-stderr.txt")
	expectedSummary := runtests.TestSummary{
		Tests: []runtests.TestDetails{{
			Name:           "fuchsia-pkg://foo#test_a",
			GNLabel:        "//a/b/c:test_a(//toolchain)",
			OutputFile:     outputFileA,
			Result:         runtests.TestFailure,
			StartTime:      start,
			DurationMillis: 5,
			DataSinks: runtests.DataSinkMap{
				"sinks": []runtests.DataSink{
					{
						Name: "SINK_A1",
						File: "sink_a1.txt",
					},
					{
						Name: "SINK_A2",
						File: "sink_a2.txt",
					},
				},
			},
		}, {
			Name:           "test_b",
			GNLabel:        "//a/b/c:test_b(//toolchain)",
			OutputFile:     outputFileB,
			Result:         runtests.TestSuccess,
			StartTime:      start,
			DurationMillis: 10,
			DataSinks: runtests.DataSinkMap{
				"sinks": []runtests.DataSink{
					{
						Name: "SINK_B",
						File: "sink_b.txt",
					},
				},
			},
		}},
	}

	summaryBytes, err := json.Marshal(&expectedSummary)
	if err != nil {
		t.Fatalf("failed to marshal expected summary: %v", err)
	}

	expectedSinks := map[string]string{
		"sink_a1.txt": "SINK_A1",
		"sink_a2.txt": "SINK_A2",
		"sink_b.txt":  "SINK_B",
	}

	// Populate all of the expected output files.
	expectedContents := map[string]string{
		outputFileA:    "STDOUT_A",
		outputFileB:    "STDERR_B",
		"summary.json": string(summaryBytes),
	}
	for name, content := range expectedSinks {
		// Add sinks to expectedContents.
		expectedContents[name] = content
		path := filepath.Join(o.outDir, name)
		dir := filepath.Dir(path)
		if err := os.MkdirAll(dir, os.ModePerm); err != nil {
			t.Fatalf("failed to make directory %q for outputs: %v", dir, err)
		}
		if err := ioutil.WriteFile(path, []byte(content), 0444); err != nil {
			t.Fatalf("failed to write contents %q to file %q: %v", content, name, err)
		}
	}

	for _, result := range results {
		if err := o.record(result); err != nil {
			t.Fatalf("failed to record result of %q: %v", result.Name, err)
		}
	}
	o.Close()

	// Verify that the summary as expected.
	actualSummary := o.summary
	if !reflect.DeepEqual(actualSummary, expectedSummary) {
		t.Errorf("unexpected summary:\nexpected: %v\nactual: %v\n", expectedSummary, actualSummary)
	}

	// Verify that the TAP output is as expected.
	expectedTAPOutput := strings.TrimSpace(`
TAP version 13
1..2
not ok 1 fuchsia-pkg://foo#test_a (5ms)
ok 2 test_b (10ms)
`)
	actualTAPOutput := strings.TrimSpace(buf.String())
	if actualTAPOutput != expectedTAPOutput {
		t.Errorf("unexpected TAP output:\nexpected: %v\nactual: %v\n", expectedTAPOutput, actualTAPOutput)
	}

	// Verify that the outDir's contents are as expected.
	outDirContents := make(map[string]string)
	for name := range expectedContents {
		path := filepath.Join(outDir, name)
		b, err := ioutil.ReadFile(path)
		if err != nil {
			t.Errorf("failed to read file %q in out dir: %v", path, err)
		}
		outDirContents[name] = string(b)
	}

	if !reflect.DeepEqual(expectedContents, outDirContents) {
		t.Fatalf("unexpected contents from out dir:\nexpected: %#v\nactual: %#v\n", expectedContents, outDirContents)
	}
}
