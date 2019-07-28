// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package perfcompare

import (
	"os"
	"os/exec"
	"path"
	"testing"
)

// This is a wrapper for running perfcompare's unit tests, which are
// written in Python.

func TestPerfcompare(t *testing.T) {
	fuchsiaDir, found := os.LookupEnv("FUCHSIA_DIR")
	if !found {
		t.SkipNow()
	}
	pytest := path.Join(fuchsiaDir, "garnet/bin/perfcompare/perfcompare_test.py")
	// This assumes that vpython is present in PATH.  That is true
	// on the bots, where vpython is also used by Fuchsia infra.
	// vpython is used for downloading Python library dependencies
	// for perfcompare.py.
	cmd := exec.Command("vpython", pytest)
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Errorf("perfcompare_test failed: %v", err)
	}
	t.Log(string(output[:]))
}
