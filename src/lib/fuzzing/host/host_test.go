// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzzing

import (
	"flag"
	"os/exec"
	"path/filepath"
	"testing"
)

// This is only used when run via `go test`
const relativePath = "../../../../scripts/fuzzing"

var testDataDir = flag.String("test-data-dir", relativePath, "Path to `fx fuzz` files; only used in GN build")

func TestFxFuzzLib(t *testing.T) {
	pyTestDir := filepath.Join(*testDataDir, "test")
	cmd := exec.Command("python", "-m", "unittest", "discover", "-s", pyTestDir, "-p", "*_test.py", "-b")
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Errorf("test failed: %v", err)
	}
	t.Log(string(output[:]))
}
