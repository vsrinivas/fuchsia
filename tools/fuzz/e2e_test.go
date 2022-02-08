// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz_test

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/golang/glog"
	"go.fuchsia.dev/fuchsia/tools/fuzz"
)

// Runs a command and returns stdout
func runCommand(t *testing.T, args ...string) string {
	return runCommandWithExpectation(t, true /* shouldSucceed */, args...)
}

func runCommandWithExpectation(t *testing.T, shouldSucceed bool, args ...string) string {
	cmd, err := fuzz.ParseArgs(args)
	if err != nil {
		t.Fatalf("Error parsing args: %s", err)
	}

	var buf bytes.Buffer
	if err := cmd.Execute(&buf); (err == nil) != shouldSucceed {
		t.Fatalf("Unexpected result executing command: %s", err)
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

	// Fetch debug logs
	log_postboot := runCommand(t, "get_logs", "-handle", handle)
	glog.Info(log_postboot)
	if !strings.Contains(log_postboot, "{{{reset}}}") {
		t.Fatalf("Post-boot debug log missing expected content:\n%s", out)
	}

	fuzzer := "example-fuzzers/out_of_memory_fuzzer"
	out = runCommand(t, "list_fuzzers", "-handle", handle)
	if !strings.Contains(out, fuzzer) {
		t.Fatalf("%q fuzzer missing from output:\n%s", fuzzer, out)
	}

	fuzzer = "example-fuzzers/crash_fuzzer"

	// Make a tempdir for holding local files
	dir := t.TempDir()

	// Ensure put_data will succeed for prepared fuzzer, even if not yet run
	out = runCommand(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", fuzzer)
	glog.Info(out)

	tmpFile := path.Join(dir, "autoexec.bat")
	if err := ioutil.WriteFile(tmpFile, []byte("something"), 0o600); err != nil {
		t.Fatalf("error creating tempfile: %s", err)
	}

	out = runCommand(t, "put_data", "-handle", handle, "-fuzzer", fuzzer,
		"-src", tmpFile, "-dst", "data/subdir/")
	glog.Info(out)
	os.Remove(tmpFile)

	out = runCommand(t, "get_data", "-handle", handle, "-fuzzer", fuzzer,
		"-src", "data/subdir/autoexec.bat", "-dst", dir)
	glog.Info(out)

	// Ensure a second call to prepare resets persistent data
	out = runCommand(t, "prepare_fuzzer", "-handle", handle, "-fuzzer", fuzzer)
	glog.Info(out)

	out = runCommandWithExpectation(t, false, "get_data", "-handle", handle, "-fuzzer", fuzzer,
		"-src", "data/subdir/autoexec.bat", "-dst", dir)
	glog.Info(out)

	// Test basic fuzzing run
	artifactDir := filepath.Join(dir, "artifacts")
	if err := os.Mkdir(artifactDir, 0o700); err != nil {
		t.Fatal(err)
	}
	out = runCommand(t, "run_fuzzer", "-handle", handle, "-fuzzer", fuzzer,
		"-artifact-dir", artifactDir, "--", "-artifact_prefix=data/", "-jobs=0")

	glog.Info(out)

	// TODO(fxbug.dev/45425): validate output more
	if !strings.Contains(out, "deadly signal") {
		t.Fatalf("output missing signal: %s", out)
	}

	// This format is emitted by the Go symbolizer, but not the C++ symbolizer
	if !strings.Contains(out, "(anonymous namespace)::crasher") {
		t.Fatalf("stack trace missing expected symbol: %s", out)
	}

	artifactRegex := regexp.MustCompile(`Test unit written to (\S+)`)
	m := artifactRegex.FindStringSubmatch(out)
	if m == nil {
		t.Fatalf("output missing artifact: %s", out)
	}

	artifactPath := m[1]
	if path.Dir(artifactPath) != artifactDir {
		t.Fatalf("artifact path not properly rewritten: %q", artifactPath)
	}

	artifactData, err := ioutil.ReadFile(artifactPath)
	if err != nil {
		t.Fatalf("error reading fetched artifact file: %s", err)
	}
	if !bytes.HasPrefix(artifactData, []byte("HI!")) {
		t.Fatalf("artifact contents unexpected: %q", artifactData)
	}

	// Attempt repro, with ASAN crash
	runCommand(t, "prepare_fuzzer", "-handle", handle, "-fuzzer",
		"example-fuzzers/overflow_fuzzer")
	reproFile := path.Join(dir, "crasher")
	reproFileContents := make([]byte, 12)
	binary.LittleEndian.PutUint64(reproFileContents, 2)
	binary.LittleEndian.PutUint32(reproFileContents[8:], 0x41414141)
	if err := ioutil.WriteFile(reproFile, reproFileContents, 0o600); err != nil {
		t.Fatalf("error creating repro file: %s", err)
	}
	runCommand(t, "put_data", "-handle", handle, "-fuzzer", "example-fuzzers/overflow_fuzzer",
		"-src", reproFile, "-dst", "data/")
	os.Remove(reproFile)

	out = runCommand(t, "run_fuzzer", "-handle", handle, "-fuzzer",
		"example-fuzzers/overflow_fuzzer", "--", "data/crasher")
	glog.Info(out)
	if m, err := regexp.MatchString(`heap-buffer-overflow`, out); err != nil || !m {
		t.Fatalf("output missing ASAN crash: %s", out)
	}

	// Ensure that the debug logs have grown in length, to verify that we are
	// properly capturing logs after early boot. The above repro of an ASAN
	// crash is guaranteed to write to the debuglog due to the current
	// definition of `__sanitizer_log_write`. This behavior may change in the
	// future, but for now it is the most straightforward way to trigger a
	// write to the log.
	log_postrun := runCommand(t, "get_logs", "-handle", handle)
	if len(log_postrun) <= len(log_postboot) {
		t.Fatalf("Post-run debug log same size as post-boot log (%d bytes)", len(log_postrun))
	}

	// Attempt repro, that shouldn't crash
	runCommand(t, "prepare_fuzzer", "-handle", handle, "-fuzzer",
		"example-fuzzers/overflow_fuzzer")
	binary.LittleEndian.PutUint64(reproFileContents, 8)
	binary.LittleEndian.PutUint32(reproFileContents[8:], 0x41414141)
	if err := ioutil.WriteFile(reproFile, reproFileContents, 0o600); err != nil {
		t.Fatalf("error creating repro file: %s", err)
	}
	runCommand(t, "put_data", "-handle", handle, "-fuzzer", "example-fuzzers/overflow_fuzzer",
		"-src", reproFile, "-dst", "data/")
	os.Remove(reproFile)

	out = runCommand(t, "run_fuzzer", "-handle", handle, "-fuzzer",
		"example-fuzzers/overflow_fuzzer", "--", "data/crasher")
	glog.Info(out)

	// TODO(fxbug.dev/45425): check exit codes

	// Test put/get
	testFile := path.Join(dir, "test_file")
	testFileContents := []byte("test file contents!")
	if err := ioutil.WriteFile(testFile, testFileContents, 0o600); err != nil {
		t.Fatalf("error creating test file: %s", err)
	}
	out = runCommand(t, "put_data", "-handle", handle, "-fuzzer", fuzzer,
		"-src", testFile, "-dst", "data/")
	glog.Info(out)

	os.Remove(testFile)
	out = runCommand(t, "get_data", "-handle", handle, "-fuzzer", fuzzer,
		"-src", "data/test_file", "-dst", dir)
	glog.Info(out)

	retrievedContents, err := ioutil.ReadFile(testFile)
	if err != nil {
		t.Fatalf("error reading fetched test file: %s", err)
	}
	if !bytes.Equal(retrievedContents, testFileContents) {
		t.Fatalf("test files do not match: sent '%s', received '%s'",
			testFileContents, retrievedContents)
	}

	// Test bulk putting a lot of small files
	corpusDir := path.Join(dir, "corpus")
	if err := os.Mkdir(corpusDir, 0o700); err != nil {
		t.Fatal(err)
	}
	for j := 0; j < 2000; j++ {
		corpusPath := path.Join(corpusDir, fmt.Sprintf("corpus-%d", j))
		if err := ioutil.WriteFile(corpusPath, []byte(fmt.Sprintf("%d", j)), 0o600); err != nil {
			t.Fatalf("error creating local corpus file: %s", err)
		}
	}
	out = runCommand(t, "put_data", "-handle", handle, "-fuzzer", fuzzer,
		"-src", corpusDir+"/*", "-dst", "data/corpus")
}
