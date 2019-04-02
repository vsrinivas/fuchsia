// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"fmt"
	"io"
	"io/ioutil"
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

// Close implements io.WriteCloser.
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

func checkTarContents(contents []byte, expectedTarFileContents map[string][]byte) error {
	var contentsBuffer bytes.Buffer
	contentsBuffer.Write(contents)
	// Open and iterate through the files in the archive.
	gzf, err := gzip.NewReader(&contentsBuffer)
	if err != nil {
		return fmt.Errorf("reading tarball failed, %v", err)
	}

	tr := tar.NewReader(gzf)
	actualTarFileContents := make(map[string][]byte)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break // End of archive.
		}
		if err != nil {
			return fmt.Errorf("reading tarball failed, %v", err)
		}
		actualData := make([]byte, hdr.Size)
		if _, err := tr.Read(actualData); err != nil && err != io.EOF {
			return fmt.Errorf("reading tarball data failed, %v", err)
		}
		actualTarFileContents[hdr.Name] = actualData
	}

	for expectedFileName, expectedFileContents := range expectedTarFileContents {
		actualFileContents, exist := actualTarFileContents[expectedFileName]
		if !exist {
			return fmt.Errorf("expecting %s to be found in tarball but not found", expectedFileName)
		}
		if !bytes.Equal(actualFileContents, expectedFileContents) {
			return fmt.Errorf("expecting contents in %s to be found same but not", expectedFileName)
		}
	}
	return nil
}

func TestRunCommand(t *testing.T) {
	// Expects dump_breakpad_symbols to produce the specified summary and
	// ninja depfile from the given input sources.
	expectOutputs := func(t *testing.T, inputs []*FakeFile, expectedDepFile string, expectedTarFileContents map[string][]byte, outDir string) {
		depFile := NewFakeFile("deps.d", "")
		tarFile := NewFakeFile("breakpad_symbols.tar.gz", "")

		// Callback to mock executing the breakpad dump_syms binary.
		execDumpSyms := func(args []string) ([]byte, error) {
			return []byte("FAKE_SYMBOL_DATA_LINE_1\nFAKE_SYMBOL_DATA_LINE_2"), nil
		}

		// Callback to perform file I/O.
		createFile := func(path string) (io.ReadWriteCloser, error) {
			return NewFakeFile(path, ""), nil
		}

		// Convert input files from `[]FakeFile` to `[]io.Reader`.
		fileReaders := make([]io.Reader, len(inputs))
		for i := range inputs {
			fileReaders[i] = inputs[i]
		}

		// Process the input files.
		summary := processIdsFiles(fileReaders, outDir, execDumpSyms, createFile)

		// Write the tarball file.
		if err := writeTarball(tarFile, summary, outDir); err != nil {
			t.Fatalf("failed to write tarball %s: %v", tarFile.Name(), err)
		}

		// Extract input filepaths.
		inputPaths := make([]string, len(inputs))
		for i := range inputs {
			inputPaths[i] = inputs[i].Name()
		}

		// Write the dep file.
		if err := writeDepFile(depFile, tarFile.Name(), inputPaths); err != nil {
			t.Fatalf("failed to write depfile %s: %v", depFile.Name(), err)
		}

		// Expect matching depfile.
		actualDepFile, err := ioutil.ReadAll(depFile)
		if err != nil {
			t.Fatal(err)
		}
		if string(actualDepFile) != expectedDepFile {
			t.Errorf("expected depfile: %s. Got %s", expectedDepFile, actualDepFile)
		}

		// Expect matching tarball.
		actualTarFile, err := ioutil.ReadAll(tarFile)
		if err != nil {
			t.Fatal(err)
		}
		if err := checkTarContents(actualTarFile, expectedTarFileContents); err != nil {
			t.Fatalf("%v", err)
		}
	}

	t.Run("should produce an emtpy summary if no data is provided", func(t *testing.T) {
		inputSources := []*FakeFile{
			NewFakeFile("idsA.txt", ""),
			NewFakeFile("idsB.txt", ""),
		}
		expectedDepFile := fmt.Sprintf("breakpad_symbols.tar.gz: %s %s\n", "idsA.txt", "idsB.txt")
		expectOutputs(t, inputSources, expectedDepFile, make(map[string][]byte), "out/")
	})
}

// No tests to verify the output of dump_syms. Assume it does the right thing.
