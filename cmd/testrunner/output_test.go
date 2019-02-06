// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"bytes"
	"io"
	"reflect"
	"strings"
	"testing"

	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/testrunner"
)

func TestTarOutput(t *testing.T) {
	tests := []struct {
		name             string
		input            testrunner.TestResult
		expectedHeader   *tar.Header
		expectedContents string
	}{
		{
			name: "archive entry for test_a",
			input: testrunner.TestResult{
				Name:   "test_a",
				Stdout: []byte("the test passed"),
				Result: runtests.TestSuccess,
			},
			expectedHeader: &tar.Header{
				Typeflag: tar.TypeReg,
				Name:     "test_a/stdout-and-stderr.txt",
				Size:     int64(len("the test passed")),
				Mode:     0666,
				Format:   tar.FormatUSTAR,
			},
			expectedContents: "the test passed",
		},
		{
			name: "archive entry for test_b",
			input: testrunner.TestResult{
				Name:   "test_b",
				Stdout: []byte("the test failed"),
				Result: runtests.TestSuccess,
			},
			expectedHeader: &tar.Header{
				Typeflag: tar.TypeReg,
				Name:     "test_b/stdout-and-stderr.txt",
				Size:     int64(len("the test failed")),
				Mode:     0666,
				Format:   tar.FormatUSTAR,
			},
			expectedContents: "the test failed",
		},
	}

	// Helper to compare archive headers. Certain fields are ignored.
	headersEqual := func(a *tar.Header, b *tar.Header) bool {
		return a.Format == b.Format &&
			a.Gid == b.Gid &&
			a.Gname == b.Gname &&
			a.Linkname == b.Linkname &&
			a.Mode == b.Mode &&
			a.Name == b.Name &&
			a.Size == b.Size &&
			a.Typeflag == b.Typeflag &&
			a.Uid == b.Uid &&
			a.Uname == b.Uname
	}

	for _, tt := range tests {
		// Record output.
		t.Run(tt.name, func(t *testing.T) {
			var buf bytes.Buffer
			to := TarOutput{w: tar.NewWriter(&buf)}
			to.Record(tt.input)
			to.Close()

			// Check the contents of the tar archive.
			tr := tar.NewReader(&buf)
			hdr, err := tr.Next()
			if err != nil {
				t.Fatalf("got an error, wanted a header: %v", err)
			}

			if !headersEqual(hdr, tt.expectedHeader) {
				t.Errorf("got:\n%+v\nwanted:\n%+v", hdr, tt.expectedHeader)
			}

			var actualContents bytes.Buffer
			if _, err := io.Copy(&actualContents, tr); err != nil {
				t.Fatalf("failed to read from the Tar Reader: %v", err)
			}

			if tt.expectedContents != actualContents.String() {
				t.Errorf("got: %q, but wanted: %q", actualContents.String(), tt.expectedContents)
			}
		})
	}
}

func TestTapOutput(t *testing.T) {
	inputs := []testrunner.TestResult{{
		Name:   "test_a",
		Result: runtests.TestSuccess,
	}, {
		Name:   "test_b",
		Result: runtests.TestFailure,
	}}

	var buf bytes.Buffer
	output := NewTAPOutput(&buf, 10)
	for _, input := range inputs {
		output.Record(input)
	}

	expectedOutput := strings.TrimSpace(`
TAP version 13
1..10
ok 1 test_a
not ok 2 test_b
`)

	actualOutput := strings.TrimSpace(buf.String())
	if actualOutput != expectedOutput {
		t.Errorf("got\n%q\nbut wanted\n%q\n", actualOutput, expectedOutput)
	}
}

func TestSummaryOutput(t *testing.T) {
	inputs := []testrunner.TestResult{{
		Name:   "test_a",
		Result: runtests.TestFailure,
	}, {
		Name:   "test_b",
		Result: runtests.TestSuccess,
	}}

	var output SummaryOutput
	for _, input := range inputs {
		output.Record(input)
	}

	expectedSummary := runtests.TestSummary{
		Tests: []runtests.TestDetails{{
			Name:       "test_a",
			OutputFile: "test_a/stdout-and-stderr.txt",
			Result:     runtests.TestFailure,
		}, {
			Name:       "test_b",
			OutputFile: "test_b/stdout-and-stderr.txt",
			Result:     runtests.TestSuccess,
		}},
	}

	actualSummary := output.Summary

	if !reflect.DeepEqual(actualSummary, expectedSummary) {
		t.Errorf("got\n%q\nbut wanted\n%q\n", actualSummary, expectedSummary)
	}
}
