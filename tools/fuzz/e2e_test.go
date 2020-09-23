// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz_test

import (
	"bytes"
	"io/ioutil"
	"os"
	"path"
	"regexp"
	"strings"
	"testing"

	"github.com/golang/glog"
	"go.fuchsia.dev/fuchsia/tools/fuzz"
)

// Runs a command and returns stdout
func runCommand(t *testing.T, args ...string) string {
	cmd, err := fuzz.ParseArgs(args)
	if err != nil {
		t.Fatalf("Error parsing args: %s", err)
	}

	var buf bytes.Buffer
	if err := cmd.Execute(&buf); err != nil {
		t.Fatalf("Error executing command: %s", err)
	}

	return buf.String()
}

func TestEndToEnd(t *testing.T) {
	if testing.Short() {
		t.Skip("skipping e2e test in short mode")
	}

	if v, ok := os.LookupEnv("FUCHSIA_DIR"); v == "" {
		// Likely running in "go test" mode.
		os.Setenv("FUCHSIA_DIR", "../..")
		if !ok {
			defer os.Unsetenv("FUCHSIA_DIR")
		} else {
			defer os.Setenv("FUCHSIA_DIR", v)
		}
		// If "../../out/default.zircon/tools/fvm" is not present, print out an
		// error message to run fx build.
	}

	out := runCommand(t, "version")
	if m, err := regexp.MatchString(`^v\d+\.\d+\.\d+\n$`, out); err != nil || !m {
		t.Fatalf("unxpected output: %s", out)
	}

	out = runCommand(t, "start_instance")
	if m, err := regexp.MatchString(`^\S+\n$`, out); err != nil || !m {
		t.Fatalf("unxpected output: %s", out)
	}

	handle := strings.TrimSuffix(out, "\n")

	defer runCommand(t, "stop_instance", "-handle", handle)

	fuzzer := "example_fuzzers/trap_fuzzer"
	out = runCommand(t, "list_fuzzers", "-handle", handle)
	if !strings.Contains(out, fuzzer) {
		t.Fatalf("fuzzer missing from output: %s", out)
	}

	// Make a tempdir for holding local files
	dir, err := ioutil.TempDir("", "e2e_test")
	if err != nil {
		t.Fatal(err)
	}

	defer os.RemoveAll(dir)

	out = runCommand(t, "run_fuzzer", "-handle", handle, "-fuzzer", fuzzer,
		"--", "-artifact_prefix=data/", "-jobs=0")

	glog.Info(out)

	// TODO(fxbug.dev/45425): validate output more
	if m, err := regexp.MatchString(`deadly signal`, out); err != nil || !m {
		t.Fatalf("unxpected output: %s", out)
	}

	testFile := path.Join(dir, "test_file")
	testFileContents := []byte("test file contents!")
	if err := ioutil.WriteFile(testFile, testFileContents, 0600); err != nil {
		t.Fatalf("error creating test file: %s", err)
	}
	out = runCommand(t, "put_data", "-handle", handle, "-fuzzer", fuzzer,
		"-src", testFile, "-dst", "/tmp/")
	glog.Info(out)

	os.Remove(testFile)
	out = runCommand(t, "get_data", "-handle", handle, "-fuzzer", fuzzer,
		"-src", "/tmp/test_file", "-dst", dir)
	glog.Info(out)

	retrievedContents, err := ioutil.ReadFile(testFile)
	if err != nil {
		t.Fatalf("error reading fetched test file: %s", err)
	}
	if !bytes.Equal(retrievedContents, testFileContents) {
		t.Fatalf("test files do not match: sent '%s', received '%s'",
			testFileContents, retrievedContents)
	}
}
