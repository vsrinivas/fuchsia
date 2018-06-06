// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main_test

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"testing"

	cmd "fuchsia.googlesource.com/tools/cmd/dump_breakpad_symbols"
)

// TODO(kjharland): Inject an io.Writer in the main package to avoid writing to
// disk in all of these tests.

func TestParseFlags(t *testing.T) {
	// Parses flags from the given command line args.
	parseFlags := func(args []string) error {
		// Prepend the command name to args to avoid having to repeat it
		// in every test case
		args = append([]string{"dump_breakpad_symbols"}, args...)
		_, _, err := cmd.ParseFlags(args)
		return err
	}

	// Expects command line parsing to succeed. Fails otherwise.
	expectSuccess := func(t *testing.T, args []string) {
		if err := parseFlags(args); err != nil {
			t.Errorf("Failed to parse command line: %+v, %v", args, err)
		}
	}

	// Expects command line parsing to fail. Fails otherwise.
	expectFailure := func(t *testing.T, args []string) {
		if parseFlags(args) == nil {
			t.Errorf("Expected error when parsing %+v. Got nil", args)
		}
	}

	t.Run("should succeed if valid arguments are given", func(t *testing.T) {
		t.Run("when there are multiple input files", func(t *testing.T) {
			expectSuccess(t, []string{
				"-out-dir=output_directory",
				"-dump-syms-path=/path/to/dump_syms",
				"ids.txt",
				"ids2.txt",
			})
		})

		t.Run("when -dry-run=true", func(t *testing.T) {
			// explicit "-dry-run=true"
			expectSuccess(t, []string{
				"-dry-run=true",
				"-out-dir=output_directory",
				"-dump-syms-path=/path/to/dump_syms",
				"ids.txt",
			})
			// Just "-dry-run"
			expectSuccess(t, []string{
				"-dry-run",
				"-out-dir=output_directory",
				"-dump-syms-path=/path/to/dump_syms",
				"ids.txt",
			})
		})

	})

	t.Run("should fail if -out-dir is missing", func(t *testing.T) {
		expectFailure(t, []string{
			"-dry-run",
			"-dump-syms-path=/path/to/dump_syms",
			"ids.txt",
		})
	})

	t.Run("should fail if -dump-syms-path is missing", func(t *testing.T) {
		expectFailure(t, []string{
			"-out-dir=output_directory",
			"-dry-run",
			"ids.txt",
		})
	})
	t.Run("should fail if input file is missing", func(t *testing.T) {
		expectFailure(t, []string{
			"-out-dir=output_directory",
			"-dry-run",
			"-dump-syms-path=/path/to/dump_syms",
		})
	})
}

func TestDepFileOuptut(t *testing.T) {
	// Create testing directory.  Use multiple input files to make sure each of
	// them is included in the dep file.  Leave them empty to emphasize that
	// their contents are not important.
	dir := createDirectory(map[string]string{
		"idsA.txt": "",
		"idsB.txt": "",
		// Empty dep file. This will get filled in by the command.
		"deps.d": "",
	})
	defer os.RemoveAll(dir.Path)

	inputFileAPath, err := filepath.Abs(dir.Entries["idsA.txt"].Name())
	if err != nil {
		t.Fatal(err)
	}
	inputFileBPath, err := filepath.Abs(dir.Entries["idsB.txt"].Name())
	if err != nil {
		t.Fatal(err)
	}
	depFilePath, err := filepath.Abs(dir.Entries["deps.d"].Name())
	if err != nil {
		t.Fatal(err)
	}

	cmd.RunMain([]string{
		"dump_fuchsia_symbols",
		"-dry-run",
		"-out-dir=/out",
		"-dump-syms-path=/path/to/dump_syms",
		"-depfile", depFilePath,
		"-summary-file", "summary.json",
		inputFileAPath,
		inputFileBPath,
	})

	depFile, err := ioutil.ReadFile(depFilePath)
	if err != nil {
		t.Fatal(err)
	}

	expectedDepFile := fmt.Sprintf("summary.json: %s %s\n", inputFileAPath, inputFileBPath)
	if string(depFile) != expectedDepFile {
		t.Fatalf("expected dep file: %s. Got %s", expectedDepFile, depFile)
	}
}

// TestingDirectory is a temporary directory that contains files for testing.
//
// Clients should always `defer os.RemoveAll(dir.Path)` when finished with this
// object.
type TestingDirectory struct {
	Path    string
	Entries map[string]*os.File
}

// Creates a directory from the given map of contents.
//
// The given contents should map the desired name of the file within the
// directory to its contents.
func createDirectory(entries map[string]string) *TestingDirectory {
	directory, err := ioutil.TempDir("", "testing")
	if err != nil {
		panic(err)
	}

	basenameToPath := make(map[string]*os.File)
	for basename, contents := range entries {
		file, err := os.Create(path.Join(directory, basename))
		if err != nil {
			panic(err)
		}
		// Write the contents.
		if _, err := file.Write([]byte(contents)); err != nil {
			panic(err)
		}
		basenameToPath[basename] = file
	}

	return &TestingDirectory{
		Path:    directory,
		Entries: basenameToPath,
	}
}

// Examples-as-tests below this line.  "Output:" at the end of each function
// verifies that what is printed on stdout matches the rest of the comment.
// If there is a difference, `go test ./...` will fail.

func ExampleWithOneInputFile() {
	// Create a testing input file with fake hash values and binary paths.
	inputFile := createTempFile("ids.txt", `
01634b09 /path/to/binaryA.elf
02298167 /path/to/binaryB
025abbbc /path/to/binaryC.so
`)

	// Run the command in dry-run mode so that we can verify the list of
	// commands that would actually run, given the test input file.
	cmd.RunMain([]string{
		"dump_breakpad_symbols",
		"-dry-run",
		"-out-dir=/out",
		"-dump-syms-path=/path/to/dump_syms",
		inputFile,
	})

	// Output:
	//{
	//   "/path/to/binaryA.elf": "/out/fe9881defb9ed1ddb9a89c38be973515f6ad7f0f.sym",
	//   "/path/to/binaryB": "/out/f03de72df78157dd14ae1cc031ddba9873947179.sym",
	//   "/path/to/binaryC.so": "/out/edbe4e45241c98dcde3538160073a0d6b097b780.sym"
	//}
}

func ExampleWithMultipleInputFiles() {
	// Create a testing input file with bogus hash values and binary paths.
	inputFileA := createTempFile("idsA.txt", `
01634b09 /path/to/binaryA.elf
02298167 /path/to/binaryB
025abbbc /path/to/binaryC.so
`)

	// Create a testing input file with bogus hash values and binary paths.
	inputFileB := createTempFile("idsB.txt", `
01634b09 /path/to/binaryD
02298167 /path/to/binaryE
025abbbc /path/to/binaryF
`)

	// Run the command in dry-run mode so that we can verify the list of
	// commands that would actually run, given the test input file.
	cmd.RunMain([]string{
		"dump_breakpad_symbols",
		"-dry-run",
		"-out-dir=/out",
		"-dump-syms-path=/path/to/dump_syms",
		inputFileA,
		inputFileB,
	})

	// Output:
	//{
	//   "/path/to/binaryA.elf": "/out/fe9881defb9ed1ddb9a89c38be973515f6ad7f0f.sym",
	//   "/path/to/binaryB": "/out/f03de72df78157dd14ae1cc031ddba9873947179.sym",
	//   "/path/to/binaryC.so": "/out/edbe4e45241c98dcde3538160073a0d6b097b780.sym",
	//   "/path/to/binaryD": "/out/8541277ee6941ac4c3c9ab2fc68edfb4c420861e.sym",
	//   "/path/to/binaryE": "/out/906bc6368e6462a6cf7b78328a675ce57ef82209.sym",
	//   "/path/to/binaryF": "/out/302cb9c3745652180c25e5da2ca3e420b8dd4e25.sym"
	//}
}

func ExampleSkipDuplicatePaths() {
	// Create a testing input file with bogus hash values and binary paths.
	inputFile := createTempFile("ids.txt", `
01634b09 /path/to/binaryA
02298167 /path/to/binaryA
`)

	// Run the command in dry-run mode so that we can verify the list of
	// commands that would actually run, given the test input file.
	cmd.RunMain([]string{
		"dump_breakpad_symbols",
		"-dry-run",
		"-out-dir=/out",
		"-dump-syms-path=/path/to/dump_syms",
		inputFile,
	})

	// Output:
	// {
	//   "/path/to/binaryA": "/out/43e5a3c9eb9829f2eb11007223de1fb0b721a909.sym"
	// }
}

// Creates a temp file with the given name and contents for testing.
//
// Returns the absolute path to the file.
func createTempFile(name, contents string) (absPath string) {
	// Create the file.
	file, err := ioutil.TempFile("", name)
	if err != nil {
		panic(err)
	}

	// Write the contents.
	file.Write([]byte(contents))

	// Grab the absolute path
	absPath, err = filepath.Abs(file.Name())
	if err != nil {
		panic(err)
	}
	return
}

// No tests to verify the output of dump_syms. Assume it does the right thing.

// TODO(kjharland): Consider creating test binaries to pass as -dump-syms-path,
// which would let us write integration tests for this file.
