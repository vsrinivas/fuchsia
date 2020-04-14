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
	"strings"
	"testing"
)

var testscript = flag.String("testscript", "", "test script to execute. Relative paths are relative to the location of the running script. Absolute paths are absolute.")

// This is a wrapper for running unit tests.
func TestExternalScript(t *testing.T) {

	var theTest = *testscript
	t.Log("Script is", theTest)
	t.Log("CWD is", filepath.Dir(os.Args[0]))
	if !strings.HasPrefix(theTest, "/") {
		dir, err := filepath.Abs(filepath.Dir(os.Args[0]))
		if err != nil {
			t.Errorf("Could not determine execution path: %v", err)
		}
		t.Log("Dir is ", dir)
		theTest = path.Join(dir, theTest)
	}

	t.Log("Running  ", theTest)
	cmd := exec.Command(theTest)
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Errorf("%v failed: %v", theTest, err)
	}
	t.Log(string(output[:]))
}
