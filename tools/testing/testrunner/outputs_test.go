// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package testrunner

import (
	"bytes"
	"context"
	"encoding/json"
	"io/ioutil"
	"net/url"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap"
)

func writeFiles(dir string, pathsToContents map[string]string) error {
	for p, contents := range pathsToContents {
		f, err := osmisc.CreateFile(filepath.Join(dir, p))
		if err != nil {
			return err
		}
		defer f.Close()
		if _, err := f.Write([]byte(contents)); err != nil {
			return err
		}
	}
	return nil
}

func TestRecordingOfOutputs(t *testing.T) {
	start := time.Unix(0, 0)
	testOutputDir := t.TempDir()
	suiteOutputDir := "suite_outputs"
	suiteOutputFile1 := filepath.Join(suiteOutputDir, "file1")
	suiteOutputFile2 := filepath.Join(suiteOutputDir, "file2")
	caseOutputFile := "case_outputs"
	origOutputs := map[string]string{
		suiteOutputFile1: "test outputs",
		suiteOutputFile2: "test outputs 2",
		caseOutputFile:   "testcase outputs",
	}
	if err := writeFiles(testOutputDir, origOutputs); err != nil {
		t.Errorf("failed to write output files: %s", err)
	}
	results := []TestResult{
		{
			Name:      "fuchsia-pkg://foo#test_a",
			GNLabel:   "//a/b/c:test_a(//toolchain)",
			Result:    runtests.TestFailure,
			StartTime: start,
			EndTime:   start.Add(5 * time.Millisecond),
			DataSinks: runtests.DataSinkReference{
				Sinks: runtests.DataSinkMap{
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
			},
			Cases: []runtests.TestCaseResult{
				{
					DisplayName: "case1",
					CaseName:    "case1",
					Status:      runtests.TestFailure,
					Format:      "FTF",
					// Test having the OutputFile be a filename.
					OutputFiles: []string{caseOutputFile},
					OutputDir:   testOutputDir,
				},
			},
			// Test having the OutputFile be a directory name.
			OutputFiles: []string{suiteOutputDir},
			OutputDir:   testOutputDir,
			Stdio:       []byte("STDOUT_A"),
		},
		{
			Name:      "test_b",
			GNLabel:   "//a/b/c:test_b(//toolchain)",
			Result:    runtests.TestSuccess,
			StartTime: start,
			EndTime:   start.Add(10 * time.Millisecond),
			Stdio:     []byte("STDERR_B"),
		},
	}

	dataDir := t.TempDir()
	outDir := filepath.Join(dataDir, "out")

	var buf bytes.Buffer
	producer := tap.NewProducer(&buf)
	producer.Plan(len(results))
	o, err := CreateTestOutputs(producer, outDir)
	if err != nil {
		t.Fatal(err)
	}
	defer o.Close()

	outputFileA := func(filename string) string {
		return filepath.Join(url.PathEscape("fuchsia-pkg//foo#test_a"), "0", filename)
	}

	outputFileB := func(filename string) string {
		return filepath.Join("test_b", "0", filename)
	}

	testAStdout := outputFileA("stdout-and-stderr.txt")
	testASuiteOutputFile1 := outputFileA(suiteOutputFile1)
	testASuiteOutputFile2 := outputFileA(suiteOutputFile2)
	testACaseOutputFile := outputFileA(filepath.Join("case1", caseOutputFile))
	testBStdout := outputFileB("stdout-and-stderr.txt")
	expectedSummary := runtests.TestSummary{
		Tests: []runtests.TestDetails{{
			Name:    "fuchsia-pkg://foo#test_a",
			GNLabel: "//a/b/c:test_a(//toolchain)",
			// The expected OutputFiles in TestSummary should contain only files.
			// If any of the TestResult OutputFiles point to directories, TestOutputs.Record()
			// should list out all the files in those directories here.
			OutputFiles:    []string{testASuiteOutputFile1, testASuiteOutputFile2, testAStdout},
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
			Cases: []runtests.TestCaseResult{
				{
					DisplayName: "case1",
					CaseName:    "case1",
					Status:      runtests.TestFailure,
					Format:      "FTF",
					OutputFiles: []string{testACaseOutputFile},
				},
			},
		}, {
			Name:           "test_b",
			GNLabel:        "//a/b/c:test_b(//toolchain)",
			OutputFiles:    []string{testBStdout},
			Result:         runtests.TestSuccess,
			StartTime:      start,
			DurationMillis: 10,
			// The data sinks will be added through a call to updateDataSinks().
			DataSinks: runtests.DataSinkMap{
				"sinks": []runtests.DataSink{
					{
						Name: "SINK_B",
						File: "other_dir/sink_b.txt",
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
		testAStdout:           "STDOUT_A",
		testBStdout:           "STDERR_B",
		testASuiteOutputFile1: origOutputs[suiteOutputFile1],
		testASuiteOutputFile2: origOutputs[suiteOutputFile2],
		testACaseOutputFile:   origOutputs[caseOutputFile],
		"summary.json":        string(summaryBytes),
	}
	for name, content := range expectedSinks {
		// Add sinks to expectedContents.
		expectedContents[name] = content
		path := filepath.Join(o.OutDir, name)
		dir := filepath.Dir(path)
		if err := os.MkdirAll(dir, 0o700); err != nil {
			t.Fatalf("failed to make directory %q for outputs: %v", dir, err)
		}
		if err := ioutil.WriteFile(path, []byte(content), 0o400); err != nil {
			t.Fatalf("failed to write contents %q to file %q: %v", content, name, err)
		}
	}

	for _, result := range results {
		if err := o.Record(context.Background(), result); err != nil {
			t.Fatalf("failed to record result of %q: %v", result.Name, err)
		}
	}
	o.updateDataSinks(map[string]runtests.DataSinkReference{
		"test_b": {
			Sinks: runtests.DataSinkMap{
				"sinks": []runtests.DataSink{
					{
						Name: "SINK_B",
						File: "sink_b.txt",
					},
				},
			},
		},
	}, "other_dir")
	o.Close()

	// Verify that the summary as expected.
	actualSummary := o.Summary
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
