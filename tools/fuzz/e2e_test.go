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

// To run this test:
// - fx set core.x64 --with-base //bundles:tests --with-base //bundles:tools --fuzz-with asan
// - fx build
// - cd tools/fuzz
// - UNDERCOAT_E2E_TESTS=yes go test -v -logtostderr
func TestEndToEnd(t *testing.T) {
	if _, found := os.LookupEnv("UNDERCOAT_E2E_TESTS"); !found {
		t.Skip("skipping end-to-end test; set UNDERCOAT_E2E_TESTS to enable")
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

	fuzzer := "example-fuzzers/crash_fuzzer"
	out = runCommand(t, "list_fuzzers", "-handle", handle)
	if !strings.Contains(out, fuzzer) {
		t.Fatalf("%q fuzzer missing from output:\n%s", fuzzer, out)
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
