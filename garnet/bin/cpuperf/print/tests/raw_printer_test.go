// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tests

import (
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestRawPrinter(t *testing.T) {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		t.Fatal(err)
	}
	outDir := filepath.Dir(testPath)
	testDataDir := filepath.Join(outDir, "test_data", "cpuperf")
	sessionSpecPath := filepath.Join(testDataDir, "raw-test.cpsession")

	got := bytes.Buffer{}
	// Pass --quiet so INFO lines, which contain source line numbers
	// and the output path prefix, won't cause erroneous failures.
	args := []string{"--session=" + sessionSpecPath, "--quiet", "--log-file=test.log"}
	runCommandWithOutputToFile(t, filepath.Join(outDir, "cpuperf_print"), args, &got)

	want, err := ioutil.ReadFile(filepath.Join(testDataDir, "raw-expected-output.txt"))
	if err != nil {
		t.Error(err)
	}
	if !bytes.Equal(want, got.Bytes()) {
		t.Fatalf("Unexpected output.\nwant:\n%s\ngot:\n%s", want, got.Bytes())
	}
}

func runCommandWithOutputToFile(t *testing.T, command string, args []string, output io.Writer) {
	// This doesn't use testing.Logf or some such because we always
	// want to see this, especially when run on bots.
	fmt.Printf("Running %s %v\n", command, args)
	cmd := exec.Command(command, args...)
	// There's no point to distinguishing stdout,stderr here.
	cmd.Stdout = output
	cmd.Stderr = output
	if err := cmd.Run(); err != nil {
		t.Fatalf("Running %s failed: %s", command, err)
	}
}
