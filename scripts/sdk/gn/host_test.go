// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gnsdk

import (
	"flag"
	"os/exec"
	"testing"
)

var testscript = flag.String("testscript", "", "test script to execute.")

// This is a wrapper for running unit tests.
func TestExternalScript(t *testing.T) {

	theTest := *testscript
	t.Log("Running  ", theTest)
	cmd := exec.Command(theTest)
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Errorf("%v failed: %v", theTest, err)
	}
	t.Log(string(output[:]))
}
