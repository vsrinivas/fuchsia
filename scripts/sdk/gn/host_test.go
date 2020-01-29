// Copyright 2019 The Fuchsia Authors. All rights reserved.
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

	pytest := *testscript
	t.Log("Running  ", pytest)
	cmd := exec.Command(pytest)
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Errorf("test_generate failed: %v", err)
	}
	t.Log(string(output[:]))
}
