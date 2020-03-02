// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package runtests

import (
	"archive/tar"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"
)

type mockClient struct {
	testResultsDir   string
	expectedContents map[string]string
}

func (c *mockClient) Read(ctx context.Context, filename string) (*bytes.Reader, error) {
	contents, ok := c.expectedContents[strings.TrimPrefix(filename, c.testResultsDir+"/")]
	if !ok {
		return nil, fmt.Errorf("%s does not exist", filename)
	}
	return bytes.NewReader([]byte(contents)), nil
}

func (c *mockClient) Write(ctx context.Context, filename string, reader io.ReaderAt, size int64) error {
	return nil
}

func (c *mockClient) RemoteAddr() *net.UDPAddr {
	return nil
}

func TestPollForSummary(t *testing.T) {
	testResultsDir, err := ioutil.TempDir("", "test-results")
	if err != nil {
		t.Fatalf("failed to create test results dir: %v", err)
	}
	defer os.RemoveAll(testResultsDir)
	outputArchive := filepath.Join(testResultsDir, "out.tar")
	outputDir := filepath.Join(testResultsDir, "out")

	outputFileA := filepath.Join("test_a", "stdout-and-stderr.txt")
	outputFileB := filepath.Join("test_b", "stdout-and-stderr.txt")
	summaryFilename := "summary.json"
	expectedSummary := TestSummary{
		Tests: []TestDetails{{
			Name:           "test_a",
			OutputFile:     outputFileA,
			Result:         TestFailure,
			DurationMillis: 5,
		}, {
			Name:           "test_b",
			OutputFile:     outputFileB,
			Result:         TestSuccess,
			DurationMillis: 10,
		}},
		Outputs: map[string]string{
			"syslog_file": "syslog.txt",
		},
	}

	summaryBytes, err := json.Marshal(&expectedSummary)
	if err != nil {
		t.Fatalf("failed to marshal expected summary: %v", err)
	}

	// Populate all of the expected output files.
	expectedContents := map[string]string{
		outputFileA:     "STDOUT_A",
		outputFileB:     "STDERR_B",
		summaryFilename: string(summaryBytes),
		"syslog.txt":    "syslog",
	}

	client := &mockClient{testResultsDir, expectedContents}

	if err := PollForSummary(context.Background(), client, summaryFilename, testResultsDir, outputArchive, "", time.Second); err != nil {
		t.Errorf("failed to tar test results: %v", err)
	}

	if err := PollForSummary(context.Background(), client, summaryFilename, testResultsDir, "", outputDir, time.Second); err != nil {
		t.Errorf("failed to copy test results to out dir: %v", err)
	}

	// Verify that the archive's contents are as expected.
	f, err := os.Open(outputArchive)
	if err != nil {
		t.Fatalf("failed to open archive %q: %v", outputArchive, err)
	}
	defer f.Close()
	tr := tar.NewReader(f)
	actualContents := make(map[string]string)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		} else if err != nil {
			t.Errorf("unexpected error in reading archive: %v", err)
		}
		content, err := ioutil.ReadAll(tr)
		if err != nil {
			t.Errorf("failed to read %q from the archive: %v", hdr.Name, err)
		}
		actualContents[hdr.Name] = string(content)
	}

	if !reflect.DeepEqual(expectedContents, actualContents) {
		t.Errorf("unexpected contents from archive:\nexpected: %#v\nactual: %#v\n", expectedContents, actualContents)
	}

	// Verify that the outputDir's contents are as expected.
	outDirContents := make(map[string]string)
	for name := range expectedContents {
		path := filepath.Join(outputDir, name)
		b, err := ioutil.ReadFile(path)
		if err != nil {
			t.Errorf("failed to read file %q in out dir: %v", path, err)
		}
		outDirContents[name] = string(b)
	}

	if !reflect.DeepEqual(expectedContents, outDirContents) {
		t.Errorf("unexpected contents from out dir:\nexpected: %#v\nactual: %#v\n", expectedContents, outDirContents)
	}
}
