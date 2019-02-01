// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tests

import (
	"io/ioutil"
	"os"
	"path"
	"testing"
)

const (
	sessionFile        string = "garnet/bin/cpuperf/print/tests/raw-test.cpsession"
	expectedOutputFile string = "garnet/bin/cpuperf/print/tests/raw-expected-output.txt"
	printerProgram     string = "cpuperf_print"
	outputTempFile            = "raw-printer-test."
)

func TestRawPrinter(t *testing.T) {
	hostBuildDir := getHostBuildDir()
	printerProgramPath := path.Join(hostBuildDir, printerProgram)
	sessionSpecPath := path.Join(fuchsiaRoot, sessionFile)
	expectedOutputPath := path.Join(fuchsiaRoot, expectedOutputFile)

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
		t.Fatalf("Error comparing output with expected output: ",
			err.Error())
	}
}
