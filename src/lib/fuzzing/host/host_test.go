// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzzing

import (
	"os"
	"os/exec"
	"path"
	"testing"
)

func TestFxFuzzLib(t *testing.T) {
	fuchsiaDir, found := os.LookupEnv("FUCHSIA_DIR")
	if !found {
		t.SkipNow()
	}
	scriptsFuzzingTests := []string{
		"cipd_test.py",
		"device_test.py",
		"fuzzer_test.py",
		"host_test.py",
	}
	for _, test := range scriptsFuzzingTests {
		pytest := path.Join(fuchsiaDir, "scripts", "fuzzing", "test", test)
		cmd := exec.Command("python", pytest)
		output, err := cmd.CombinedOutput()
		if err != nil {
			t.Errorf("%s failed:: %v", test, err)
		}
		t.Log(string(output[:]))
	}
}
