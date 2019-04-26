// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tests

import (
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"testing"
)

const (
	sessionFilename        = "raw-test.cpsession"
	expectedOutputFilename = "raw-expected-output.txt"
	printerProgram         = "cpuperf_print"
	outputTempFile         = "raw-printer-test."
)

func TestRawPrinter(t *testing.T) {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		t.Fatal(err)
	}
	outDir := filepath.Dir(testPath)
	printerProgramPath := path.Join(outDir, printerProgram)
	testDataDir := path.Join(outDir, "test_data", "cpuperf")
	sessionSpecPath := path.Join(testDataDir, sessionFilename)
	expectedOutputPath := path.Join(testDataDir, expectedOutputFilename)

	outputFile, err := ioutil.TempFile("", outputTempFile)
	if err != nil {
		t.Fatalf("Error creating output file: %s", err.Error())
	}
	defer os.Remove(outputFile.Name())
	defer outputFile.Close()

	// Pass --quiet so INFO lines, which contain source line numbers
	// and the output path prefix, won't cause erroneous failures.
	args := []string{"--session=" + sessionSpecPath, "--quiet"}
	err = runCommandWithOutputToFile(printerProgramPath, args,
		outputFile)
	if err != nil {
		t.Fatal(err)
	}

	err = compareFiles(expectedOutputPath, outputFile.Name())
	if err != nil {
		t.Fatalf("Error comparing output with expected output: %v", err)
	}
}
