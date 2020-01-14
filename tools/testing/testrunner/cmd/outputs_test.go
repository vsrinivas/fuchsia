// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package main

import (
	"archive/tar"
	"bytes"
	"encoding/json"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap/lib"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/lib"
)

func TestRecordingOfOutputs(t *testing.T) {
	start := time.Unix(0, 0)
	results := []testrunner.TestResult{
		{
			Name:      "test_a",
			GNLabel:   "//a/b/c:test_a(//toolchain)",
			Result:    runtests.TestFailure,
			StartTime: start,
			EndTime:   start.Add(5 * time.Millisecond),
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
			Stdout: []byte("STDOUT_A"),
		},
		{
			Name:      "test_b",
			GNLabel:   "//a/b/c:test_b(//toolchain)",
			Result:    runtests.TestSuccess,
			StartTime: start,
			EndTime:   start.Add(10 * time.Millisecond),
			DataSinks: runtests.DataSinkMap{
				"sinks": []runtests.DataSink{
					{
						Name: "SINK_B",
						File: "sink_b.txt",
					},
				},
			},
			Stderr: []byte("STDERR_B"),
		},
	}

	dataDir, err := ioutil.TempDir("", "testrunner_tests")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(dataDir)
	archivePath := filepath.Join(dataDir, "out.tar")

	var buf bytes.Buffer
	producer := tap.NewProducer(&buf)
	producer.Plan(len(results))
	o, err := createTestOutputs(producer, dataDir, archivePath)
	if err != nil {
		t.Fatalf("failed to create a test outputs object: %v", err)
	}
	defer o.Close()

	outputFileA := filepath.Join("test_a", "stdout-and-stderr.txt")
	outputFileB := filepath.Join("test_b", "stdout-and-stderr.txt")
	expectedSummary := runtests.TestSummary{
		Tests: []runtests.TestDetails{{
			Name:           "test_a",
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

	// Populate all of the expected output files.
	expectedContents := map[string]string{
		outputFileA:    "STDOUT_A",
		outputFileB:    "STDERR_B",
		"sink_a1.txt":  "SINK_A1",
		"sink_a2.txt":  "SINK_A2",
		"sink_b.txt":   "SINK_B",
		"summary.json": string(summaryBytes),
	}
	for name, content := range expectedContents {
		path := filepath.Join(o.dataDir, name)
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
not ok 1 test_a (5ms)
ok 2 test_b (10ms)
`)
	actualTAPOutput := strings.TrimSpace(buf.String())
	if actualTAPOutput != expectedTAPOutput {
		t.Errorf("unexpected TAP output:\nexpected: %v\nactual: %v\n", expectedTAPOutput, actualTAPOutput)
	}

	// Verify that the archive's contents are as expected.
	f, err := os.Open(archivePath)
	if err != nil {
		t.Fatalf("failed to open archive %q: %v", archivePath, err)
	}
	defer f.Close()
	tr := tar.NewReader(f)
	actualContents := make(map[string]string)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		} else if err != nil {
			t.Fatalf("unexpected error in reading archive: %v", err)
		}
		content, err := ioutil.ReadAll(tr)
		if err != nil {
			t.Fatalf("failed to read %q from the archive: %v", hdr.Name, err)
		}
		actualContents[hdr.Name] = string(content)
	}

	if !reflect.DeepEqual(expectedContents, actualContents) {
		t.Fatalf("unexpected contents from archive:\nexpected: %#v\nactual: %#v\n", expectedContents, actualContents)
	}
}
