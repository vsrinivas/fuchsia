// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"strings"
	"testing"
)

// FakeFile is a file-like io.ReadWriteCloser for testing.
type FakeFile struct {
	path string
	*bytes.Buffer
}

// The filepath of this FakeFile.
func (f *FakeFile) Name() string {
	return f.path
}

// Close implements io.WriteCloser
func (f *FakeFile) Close() error {
	// Noop
	return nil
}

func NewFakeFile(path string, contents string) *FakeFile {
	return &FakeFile{
		Buffer: bytes.NewBuffer([]byte(contents)),
		path:   path,
	}
}

func TestRunCommand(t *testing.T) {
	// Expects dump_breakpad_symbols to produce the specified summary and
	// ninja depfile from the given input sources.
	expectOutputs := func(t *testing.T, inputs []*FakeFile, expectedSummary string, expectedDepFile string) {
		depFile := NewFakeFile("deps.d", "")
		summaryFile := NewFakeFile("summary.json", "")

		// Callback to mock executing the breakpad dump_syms binary.
		execDumpSyms := func(args []string) ([]byte, error) {
			return []byte("FAKE_SYMBOL_DATA_LINE_1\nFAKE_SYMBOL_DATA_LINE_2"), nil
		}

		// Callback to perform file I/O.
		createFile := func(path string) (io.ReadWriteCloser, error) {
			return NewFakeFile(path, ""), nil
		}

		// Convert input files from `[]FakeFile` to `[]io.Reader`
		fileReaders := make([]io.Reader, len(inputs))
		for i := range inputs {
			fileReaders[i] = inputs[i]
		}
		// Process the input files.
		summary := processIdsFiles(fileReaders, "/out/", execDumpSyms, createFile)
		if err := writeSummary(summaryFile, summary); err != nil {
			t.Fatalf("failed to write summary %s: %v", summaryFile.Name(), err)
		}

		// Extract input filepaths.
		inputPaths := make([]string, len(inputs))
		for i := range inputs {
			inputPaths[i] = inputs[i].Name()
		}
		// Write the dep file
		if err := writeDepFile(depFile, summaryFile.Name(), inputPaths); err != nil {
			t.Fatalf("failed to write depfile %s: %v", depFilename, err)
		}

		// Expect matching summary.
		actualSummary, err := ioutil.ReadAll(summaryFile)
		if err != nil {
			t.Fatal(err)
		}
		if string(actualSummary) != expectedSummary {
			t.Errorf("expected summary: %s. Got %s", expectedSummary, actualSummary)
		}

		// Expect matching depfile.
		actualDepFile, err := ioutil.ReadAll(depFile)
		if err != nil {
			t.Fatal(err)
		}
		if string(actualDepFile) != expectedDepFile {
			t.Errorf("expected depfile: %s. Got %s", expectedDepFile, actualDepFile)
		}
	}

	t.Run("should produce an emtpy summary if no data is provided", func(t *testing.T) {
		inputSources := []*FakeFile{
			NewFakeFile("idsA.txt", ""),
			NewFakeFile("idsB.txt", ""),
		}
		expectedSummary := "{}"
		expectedDepFile := fmt.Sprintf("summary.json: %s %s\n", "idsA.txt", "idsB.txt")
		expectOutputs(t, inputSources, expectedSummary, expectedDepFile)
	})

	t.Run("should handle a single input file", func(t *testing.T) {
		// Create a testing input file with fake hash values and binary paths.
		inputSources := []*FakeFile{
			NewFakeFile("ids.txt", strings.TrimSpace(`
				01634b09 /path/to/binaryA.elf
				02298167 /path/to/binaryB
				025abbbc /path/to/binaryC.so
			`)),
		}
		expectedSummary, err := json.MarshalIndent(map[string]string{
			"/path/to/binaryA.elf": "/out/fe9881defb9ed1ddb9a89c38be973515f6ad7f0f.sym",
			"/path/to/binaryB":     "/out/f03de72df78157dd14ae1cc031ddba9873947179.sym",
			"/path/to/binaryC.so":  "/out/edbe4e45241c98dcde3538160073a0d6b097b780.sym",
		}, "", "  ")

		if err != nil {
			t.Fatal(err)
		}
		expectedDepFile := "summary.json: ids.txt\n"
		expectOutputs(t, inputSources, string(expectedSummary), expectedDepFile)
	})

	t.Run("should handle multiple input files", func(t *testing.T) {
		inputSources := []*FakeFile{
			NewFakeFile("idsA.txt", strings.TrimSpace(`
				01634b09 /path/to/binaryA.elf
				02298167 /path/to/binaryB
				025abbbc /path/to/binaryC.so
			`)),
			NewFakeFile("idsB.txt", strings.TrimSpace(`
				01634b09 /path/to/binaryD
				02298167 /path/to/binaryE
				025abbbc /path/to/binaryF
			`)),
		}
		expectedSummary, err := json.MarshalIndent(map[string]string{
			"/path/to/binaryA.elf": "/out/fe9881defb9ed1ddb9a89c38be973515f6ad7f0f.sym",
			"/path/to/binaryB":     "/out/f03de72df78157dd14ae1cc031ddba9873947179.sym",
			"/path/to/binaryC.so":  "/out/edbe4e45241c98dcde3538160073a0d6b097b780.sym",
			"/path/to/binaryD":     "/out/8541277ee6941ac4c3c9ab2fc68edfb4c420861e.sym",
			"/path/to/binaryE":     "/out/906bc6368e6462a6cf7b78328a675ce57ef82209.sym",
			"/path/to/binaryF":     "/out/302cb9c3745652180c25e5da2ca3e420b8dd4e25.sym",
		}, "", "  ")
		if err != nil {
			t.Fatal(err)
		}
		expectedDepFile := "summary.json: idsA.txt idsB.txt\n"

		expectOutputs(t, inputSources, string(expectedSummary), expectedDepFile)
	})

	t.Run("should skip duplicate binary paths", func(t *testing.T) {
		inputSources := []*FakeFile{
			NewFakeFile("idsA.txt", strings.TrimSpace(`
				01634b09 /path/to/binaryA
				02298167 /path/to/binaryB
				asdf87fs /path/to/binaryC
			`)),
			NewFakeFile("idsB.txt", strings.TrimSpace(`
				01634b09 /path/to/binaryA
				02298167 /path/to/binaryB
				asdf87fs /path/to/binaryC
			`)),
			NewFakeFile("idsC.txt", strings.TrimSpace(`
				01634b09 /path/to/binaryA
				02298167 /path/to/binaryB
				asdf87fs /path/to/binaryC
			`)),
		}
		expectedSummary, err := json.MarshalIndent(map[string]string{
			"/path/to/binaryA": "/out/43e5a3c9eb9829f2eb11007223de1fb0b721a909.sym",
			"/path/to/binaryB": "/out/f03de72df78157dd14ae1cc031ddba9873947179.sym",
			"/path/to/binaryC": "/out/565de70a22c63a819a959fda8b95d6f4dfc6c1de.sym",
		}, "", "  ")
		if err != nil {
			t.Fatal(err)
		}
		expectedDepFile := "summary.json: idsA.txt idsB.txt idsC.txt\n"

		expectOutputs(t, inputSources, string(expectedSummary), expectedDepFile)
	})
}

// No tests to verify the output of dump_syms. Assume it does the right thing.
