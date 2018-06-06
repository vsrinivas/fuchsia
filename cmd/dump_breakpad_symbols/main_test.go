// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main_test

import (
	"io/ioutil"
	"path/filepath"
	"testing"

	cmd "fuchsia.googlesource.com/tools/cmd/dump_breakpad_symbols"
)

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
//
// TODO(kjharland): It would be nice to create an entire temp directory, and
// create all files in that directory for testing. Then each test can
// `defer dir.delete()` to clean up.
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
