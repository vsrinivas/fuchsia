// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gnsdk

import (
	"flag"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"testing"
)

var testscript = flag.String("testscript", "", "test script to execute. Relative paths are relative to the location of the running script. Absolute paths are absolute.")
var testroot = flag.String("testroot", "", "Root directory of the files needed to execute the test.")

// This is a wrapper for running unit tests.
func TestExternalScript(t *testing.T) {

	var (
		theTest = *testscript
		theRoot = *testroot
	)
	t.Log("Script is", theTest)
	dir, err := filepath.Abs(filepath.Dir(os.Args[0]))
	if err != nil {
		t.Errorf("Could not determine execution path: %v", err)
	}
	t.Log("Script Dir is ", dir)
	if !filepath.IsAbs(theTest) {
		theTest = path.Join(dir, theTest)
	}
	if !filepath.IsAbs(theRoot) {
		theRoot = path.Join(dir, theRoot)
	}
	// Make sure the test is executable.
	if err := os.Chmod(theTest, 0755); err != nil {
		t.Errorf("Chmod %v failed: %v", theTest, err)
	}
	t.Log("Running  ", theTest, " in ", theRoot)
	cmd := exec.Command(theTest)
	cmd.Dir = theRoot
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Errorf("%v failed: %v", theTest, err)
	}
	t.Log(string(output[:]))
}
