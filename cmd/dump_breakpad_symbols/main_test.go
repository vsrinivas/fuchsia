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
	"testing"

	"fuchsia.googlesource.com/tools/elflib"
	"fuchsia.googlesource.com/tools/symbolize"
)

// TestReadWriteCloser helps mimic io.ReadWriteCloser for file-like objects during tests.
type TestReadWriteCloser struct {
	*bytes.Buffer
}

// Close implements io.WriteCloser
func (wc *TestReadWriteCloser) Close() error {
	// Noop
	return nil
}

func NewTestReadWriteCloser() io.ReadWriteCloser {
	return &TestReadWriteCloser{
		new(bytes.Buffer),
	}
}

func TestRunCommand(t *testing.T) {
	// Expects dump_breakpad_symbols to produce the specified summary and
	// ninja depfile from the given input sources.
	expectOutputs := func(inputSources []symbolize.BinaryFileSource, expectedSummary string, expectedDepFile string) {
		// The dep file to write.
		depFile := NewTestReadWriteCloser()

		// The summary file to write.
		summaryFile := NewTestReadWriteCloser()

		// Callback to mock executing the breakpad dump_syms binary.
		execDumpSyms := func(args []string) ([]byte, error) {
			return []byte("FAKE_SYMBOL_DATA_LINE_1\nFAKE_SYMBOL_DATA_LINE_2"), nil
		}

		// Mock file I/O.
		createFile := func(path string) (io.ReadWriteCloser, error) {
			return NewTestReadWriteCloser(), nil
		}

		// Do what main() does...

		// Process the IDsFiles.
		summary := processIdsFiles(inputSources, "/out/", execDumpSyms, createFile)
		summaryFilename := "summary.json"
		if err := writeSummary(summaryFile, summary); err != nil {
			t.Fatalf("failed to write summary %s: %v", summaryFilename, err)
		}
		if err := writeDepFile(depFile, summaryFilename, inputSources); err != nil {
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
		inputSources := []symbolize.BinaryFileSource{
			symbolize.NewMockSource("idsA.txt", []elflib.BinaryFileRef{}),
			symbolize.NewMockSource("idsB.txt", []elflib.BinaryFileRef{}),
		}
		expectedSummary := "{}"
		expectedDepFile := fmt.Sprintf("summary.json: %s %s\n", "idsA.txt", "idsB.txt")

		expectOutputs(inputSources, expectedSummary, expectedDepFile)
	})

	t.Run("should handle a single input file", func(t *testing.T) {
		// Create a testing input file with fake hash values and binary paths.
		inputSources := []symbolize.BinaryFileSource{
			symbolize.NewMockSource("ids.txt", []elflib.BinaryFileRef{
				{BuildID: "01634b09", Filepath: "/path/to/binaryA.elf"},
				{BuildID: "02298167", Filepath: "/path/to/binaryB"},
				{BuildID: "025abbbc", Filepath: "/path/to/binaryC.so"},
			}),
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

		expectOutputs(inputSources, string(expectedSummary), expectedDepFile)
	})

	t.Run("should handle multiple input files", func(t *testing.T) {
		inputSources := []symbolize.BinaryFileSource{
			symbolize.NewMockSource("idsA.txt", []elflib.BinaryFileRef{
				{BuildID: "01634b09", Filepath: "/path/to/binaryA.elf"},
				{BuildID: "02298167", Filepath: "/path/to/binaryB"},
				{BuildID: "025abbbc", Filepath: "/path/to/binaryC.so"},
			}),
			symbolize.NewMockSource("idsB.txt", []elflib.BinaryFileRef{
				{BuildID: "01634b09", Filepath: "/path/to/binaryD"},
				{BuildID: "02298167", Filepath: "/path/to/binaryE"},
				{BuildID: "025abbbc", Filepath: "/path/to/binaryF"},
			}),
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

		expectOutputs(inputSources, string(expectedSummary), expectedDepFile)
	})

	t.Run("should skip duplicate binary paths", func(t *testing.T) {
		inputSources := []symbolize.BinaryFileSource{
			symbolize.NewMockSource("idsA.txt", []elflib.BinaryFileRef{
				{BuildID: "01634b09", Filepath: "/path/to/binaryA"},
				{BuildID: "02298167", Filepath: "/path/to/binaryB"},
				{BuildID: "asdf87fs", Filepath: "/path/to/binaryC"},
			}),
			symbolize.NewMockSource("idsB.txt", []elflib.BinaryFileRef{
				{BuildID: "01634b09", Filepath: "/path/to/binaryA"},
				{BuildID: "02298167", Filepath: "/path/to/binaryB"},
				{BuildID: "asdf87fs", Filepath: "/path/to/binaryC"},
			}),
			symbolize.NewMockSource("idsC.txt", []elflib.BinaryFileRef{
				{BuildID: "01634b09", Filepath: "/path/to/binaryA"},
				{BuildID: "02298167", Filepath: "/path/to/binaryB"},
				{BuildID: "asdf87fs", Filepath: "/path/to/binaryC"},
			}),
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

		expectOutputs(inputSources, string(expectedSummary), expectedDepFile)
	})
}

// No tests to verify the output of dump_syms. Assume it does the right thing.
